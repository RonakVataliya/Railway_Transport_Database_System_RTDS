/**
 * ============================================================
 *  Indian Railways Database Management System
 *  Console Application — IT214 DB Project 2026
 *
 *  Backend : PostgreSQL (schema: DB_Project)
 *  Connector: libpq (PostgreSQL C library)
 *  Language : C++17
 * ============================================================
 */

#include <libpq-fe.h>
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <limits>
#include <algorithm>
#include <ctime>
#include <random>
#include <cstring>

using namespace std;

// ============================================================
//  DATABASE CONNECTION (global singleton)
// ============================================================

static PGconn* conn = nullptr;

bool connectDB(const string& host, const string& port,
               const string& dbname, const string& user,
               const string& password)
{
    string cs = "host=" + host + " port=" + port +
                " dbname=" + dbname + " user=" + user +
                " password=" + password;
    conn = PQconnectdb(cs.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        cerr << "Connection failed: " << PQerrorMessage(conn) << "\n";
        PQfinish(conn);
        return false;
    }
    PGresult* r = PQexec(conn, "SET search_path TO DB_Project");
    PQclear(r);
    return true;
}

void disconnectDB() { if (conn) PQfinish(conn); }

// ============================================================
//  INPUT HELPERS
// ============================================================

void clearInput() {
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

string getInput(const string& prompt) {
    cout << prompt;
    string s;
    getline(cin, s);
    return s;
}

int getIntInput(const string& prompt) {
    int v;
    while (true) {
        cout << prompt;
        if (cin >> v) { clearInput(); return v; }
        cout << "  [!] Please enter a valid integer.\n";
        cin.clear(); clearInput();
    }
}

double getDblInput(const string& prompt) {
    double v;
    while (true) {
        cout << prompt;
        if (cin >> v) { clearInput(); return v; }
        cout << "  [!] Please enter a valid number.\n";
        cin.clear(); clearInput();
    }
}

// Escape single-quotes to prevent SQL errors
string esc(const string& s) {
    string r;
    for (char c : s) { if (c == '\'') r += "''"; else r += c; }
    return r;
}

string nowTS() {
    time_t t = time(nullptr);
    tm* tm_ = localtime(&t);
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_);
    return string(buf);
}

string genID(const string& prefix) {
    static mt19937 rng(time(nullptr));
    uniform_int_distribution<int> d(10000, 99999);
    return prefix + to_string(d(rng));
}

// ============================================================
//  DISPLAY ENGINE  — pretty ASCII table
// ============================================================

static const int MAX_COL_W = 28;   // max display width per column

void hline(const vector<int>& w) {
    cout << "+";
    for (int x : w) cout << string(x + 2, '-') << "+";
    cout << "\n";
}

void displayResult(PGresult* res) {
    if (!res) { cout << "(null result)\n"; return; }
    ExecStatusType st = PQresultStatus(res);

    if (st == PGRES_COMMAND_OK) {
        cout << "  \033[32m✓\033[0m Done. Rows affected: " << PQcmdTuples(res) << "\n";
        return;
    }
    if (st != PGRES_TUPLES_OK) {
        cout << "  \033[31m[ERROR]\033[0m " << PQresultErrorMessage(res) << "\n";
        return;
    }

    int nr = PQntuples(res), nc = PQnfields(res);
    if (nr == 0) { cout << "  (no records found)\n"; return; }

    vector<int> w(nc);
    for (int c = 0; c < nc; c++)
        w[c] = min((int)strlen(PQfname(res, c)), MAX_COL_W);
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < nc; c++) {
            int len = PQgetisnull(res,r,c) ? 4 : (int)strlen(PQgetvalue(res,r,c));
            w[c] = max(w[c], min(len, MAX_COL_W));
        }

    hline(w);
    cout << "|";
    for (int c = 0; c < nc; c++)
        cout << " \033[1m" << left << setw(w[c]) << string(PQfname(res,c)).substr(0,w[c]) << "\033[0m |";
    cout << "\n";
    hline(w);

    for (int r = 0; r < nr; r++) {
        cout << "|";
        for (int c = 0; c < nc; c++) {
            string val = PQgetisnull(res,r,c) ? "NULL" : PQgetvalue(res,r,c);
            if ((int)val.size() > MAX_COL_W) val = val.substr(0, MAX_COL_W-3) + "...";
            cout << " " << left << setw(w[c]) << val << " |";
        }
        cout << "\n";
    }
    hline(w);
    cout << "  Total: " << nr << " row(s)\n";
}

// Execute SQL and optionally show result
bool execSQL(const string& sql, bool show = false) {
    PGresult* r = PQexec(conn, sql.c_str());
    ExecStatusType st = PQresultStatus(r);
    bool ok = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK);
    if (!ok)
        cout << "  \033[31m[SQL ERROR]\033[0m " << PQresultErrorMessage(r) << "\n";
    else if (show)
        displayResult(r);
    else if (st == PGRES_COMMAND_OK)
        cout << "  \033[32m✓\033[0m Rows affected: " << PQcmdTuples(r) << "\n";
    PQclear(r);
    return ok;
}

void qshow(const string& sql) {
    PGresult* r = PQexec(conn, sql.c_str());
    displayResult(r);
    PQclear(r);
}

// Fetch single value helper
string fetchOne(const string& sql) {
    PGresult* r = PQexec(conn, sql.c_str());
    string v = (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
               ? PQgetvalue(r, 0, 0) : "";
    PQclear(r);
    return v;
}

// ============================================================
//  VIEWS + STORED PROCEDURES  (created at startup)
// ============================================================

void setupDB() {
    // ---------- VIEWS ----------
    const char* views[] = {
        // vw_train_details
        R"(CREATE OR REPLACE VIEW vw_train_details AS
           SELECT t.train_id, t.train_name, t.train_type, t.train_status,
                  r.route_name, tc.class_name,
                  COALESCE(l.loco_class,'Not Assigned') AS loco_class,
                  t.total_coaches, t.base_fare
           FROM train t
           JOIN route r       ON t.route_id  = r.route_id
           JOIN train_class tc ON t.class_id  = tc.class_id
           LEFT JOIN locomotive l ON t.loco_id = l.loco_id)",

        // vw_active_schedules
        R"(CREATE OR REPLACE VIEW vw_active_schedules AS
           SELECT s.train_id, t.train_name,
                  TO_CHAR(s.starting_ts,'YYYY-MM-DD HH24:MI') AS departure,
                  src.station_name AS source_station,
                  dst.station_name AS destination_station,
                  s.status,
                  COALESCE(s.running_time::text,'N/A') AS running_time
           FROM schedule s
           JOIN train   t   ON s.train_id              = t.train_id
           JOIN station src ON s.source_station_id     = src.station_id
           JOIN station dst ON s.destination_station_id= dst.station_id)",

        // vw_ticket_details
        R"(CREATE OR REPLACE VIEW vw_ticket_details AS
           SELECT tk.pnr, u.name AS user_name, u.email,
                  t.train_name,
                  src.station_name AS boarding,
                  dst.station_name AS destination,
                  tk.passenger_count,
                  TO_CHAR(tk.booking_ts,'YYYY-MM-DD HH24:MI') AS booked_at,
                  p.amount, p.method AS pay_method
           FROM tickets tk
           JOIN registered_user u ON tk.user_id          = u.user_id
           JOIN schedule s        ON tk.schedule_train_id = s.train_id
                                 AND tk.schedule_starting_ts = s.starting_ts
           JOIN train  t          ON s.train_id           = t.train_id
           JOIN station src       ON tk.boarding_station  = src.station_id
           JOIN station dst       ON tk.destination_station=dst.station_id
           JOIN payment p         ON tk.transaction_id    = p.transaction_id)",

        // vw_staff_details
        R"(CREATE OR REPLACE VIEW vw_staff_details AS
           SELECT st.staff_id, st.name, st.role, st.salary,
                  z.zone_name, d.division_name, dept.dept_name,
                  COALESCE(stn.station_name,'N/A') AS station
           FROM staff st
           JOIN zone       z    ON st.zone_id     = z.zone_id
           JOIN division   d    ON st.division_id  = d.division_id
           JOIN department dept ON st.dept_id      = dept.dept_id
           LEFT JOIN station stn ON st.station_id  = stn.station_id)",

        // vw_maintenance_summary
        R"(CREATE OR REPLACE VIEW vw_maintenance_summary AS
           SELECT mr.maintenance_id, t.train_name,
                  mr.maintenance_date::text, mr.maintenance_type,
                  mr.maintenance_status,
                  COALESCE(mr.description,'—') AS description,
                  COUNT(ms.staff_id) AS staff_assigned
           FROM maintenance_record mr
           JOIN train t ON mr.train_id = t.train_id
           LEFT JOIN maintenance_staff ms ON mr.maintenance_id = ms.maintenance_id
           GROUP BY mr.maintenance_id, t.train_name, mr.maintenance_date,
                    mr.maintenance_type, mr.maintenance_status, mr.description)",

        // vw_live_status
        R"(CREATE OR REPLACE VIEW vw_live_status AS
           SELECT lts.schedule_train_id AS train_id, t.train_name,
                  TO_CHAR(lts.schedule_starting_ts,'YYYY-MM-DD HH24:MI') AS scheduled_dep,
                  s.station_name, s.city,
                  lts.delay_minutes,
                  COALESCE(lts.delay_reason,'On Time') AS reason,
                  lts.status,
                  TO_CHAR(lts.reported_time,'YYYY-MM-DD HH24:MI') AS last_updated
           FROM live_train_status lts
           JOIN train   t ON lts.schedule_train_id = t.train_id
           JOIN station s ON lts.station_id        = s.station_id)",

        // vw_route_stations
        R"(CREATE OR REPLACE VIEW vw_route_stations AS
           SELECT r.route_id, r.route_name, r.total_distance,
                  rs.visiting_order, s.station_id, s.station_name,
                  s.city, s.state, rs.platform_no,
                  rs.arrival_time::text   AS arrival,
                  rs.departure_time::text AS departure,
                  rs.distance_from_source
           FROM route r
           JOIN route_station rs ON r.route_id   = rs.route_id
           JOIN station       s  ON rs.station_id = s.station_id
           ORDER BY r.route_id, rs.visiting_order)",

        nullptr
    };

    // ---------- STORED FUNCTIONS ----------
    const char* funcs[] = {
        // fn_get_train_occupancy
        R"(CREATE OR REPLACE FUNCTION fn_get_train_occupancy(
               p_train_id    VARCHAR,
               p_starting_ts TIMESTAMP)
           RETURNS TABLE(
               coach_code      VARCHAR,
               coach_type      VARCHAR,
               total_seats     INT,
               booked_seats    BIGINT,
               available_seats BIGINT)
           LANGUAGE plpgsql AS $$
           BEGIN
               RETURN QUERY
               SELECT c.coach_code, c.coach_type, c.total_seats,
                      COUNT(p.passenger_id)                       AS booked_seats,
                      c.total_seats - COUNT(p.passenger_id)       AS available_seats
               FROM coach c
               LEFT JOIN seat se ON c.train_id = se.train_id AND c.coach_code = se.coach_code
               LEFT JOIN passenger p ON  se.train_id    = p.train_id
                                     AND se.coach_code  = p.coach_code
                                     AND se.seat_number = p.seat_number
                                     AND p.pnr IN (
                                           SELECT pnr FROM tickets
                                           WHERE schedule_train_id   = p_train_id
                                             AND schedule_starting_ts = p_starting_ts)
               WHERE c.train_id = p_train_id
               GROUP BY c.coach_code, c.coach_type, c.total_seats;
           END; $$)",

        // fn_revenue_by_train
        R"(CREATE OR REPLACE FUNCTION fn_revenue_by_train()
           RETURNS TABLE(
               train_id      VARCHAR,
               train_name    VARCHAR,
               total_tickets BIGINT,
               total_revenue NUMERIC)
           LANGUAGE plpgsql AS $$
           BEGIN
               RETURN QUERY
               SELECT t.train_id, t.train_name,
                      COUNT(tk.pnr)                  AS total_tickets,
                      COALESCE(SUM(p.amount), 0.00)  AS total_revenue
               FROM train t
               LEFT JOIN tickets tk ON tk.schedule_train_id = t.train_id
               LEFT JOIN payment  p ON tk.transaction_id    = p.transaction_id
               GROUP BY t.train_id, t.train_name
               ORDER BY total_revenue DESC;
           END; $$)",

        // fn_salary_stats
        R"(CREATE OR REPLACE FUNCTION fn_salary_stats()
           RETURNS TABLE(
               dept_name      VARCHAR,
               employee_count BIGINT,
               avg_salary     NUMERIC,
               min_salary     NUMERIC,
               max_salary     NUMERIC,
               total_payroll  NUMERIC)
           LANGUAGE plpgsql AS $$
           BEGIN
               RETURN QUERY
               SELECT dept.dept_name,
                      COUNT(s.staff_id)          AS employee_count,
                      ROUND(AVG(s.salary), 2)    AS avg_salary,
                      MIN(s.salary)              AS min_salary,
                      MAX(s.salary)              AS max_salary,
                      SUM(s.salary)              AS total_payroll
               FROM staff s
               JOIN department dept ON s.dept_id = dept.dept_id
               GROUP BY dept.dept_name
               ORDER BY total_payroll DESC;
           END; $$)",

        // fn_cancel_ticket
        R"(CREATE OR REPLACE FUNCTION fn_cancel_ticket(
               p_pnr        VARCHAR,
               p_reason     VARCHAR,
               p_refund_pct NUMERIC)
           RETURNS TEXT
           LANGUAGE plpgsql AS $$
           DECLARE
               v_amount    NUMERIC;
               v_refund    NUMERIC;
               v_refund_id VARCHAR;
           BEGIN
               SELECT p.amount INTO v_amount
               FROM tickets tk JOIN payment p ON tk.transaction_id = p.transaction_id
               WHERE tk.pnr = p_pnr;

               IF NOT FOUND THEN
                   RETURN 'ERROR: Ticket not found';
               END IF;

               IF EXISTS (SELECT 1 FROM ticket_refund WHERE pnr = p_pnr) THEN
                   RETURN 'ERROR: Ticket already cancelled / refunded';
               END IF;

               v_refund    := ROUND(v_amount * p_refund_pct / 100.0, 2);
               v_refund_id := 'REF' || TO_CHAR(NOW(),'YYYYMMDDHH24MISS');

               INSERT INTO ticket_refund(refund_id, pnr, reason_code, refund_amount, refund_date)
               VALUES (v_refund_id, p_pnr, p_reason, v_refund, CURRENT_DATE);

               RETURN 'SUCCESS: Refund of INR ' || v_refund ||
                      ' processed. Refund ID: ' || v_refund_id;
           END; $$)",

        // fn_zone_summary  (analytical report)
        R"(CREATE OR REPLACE FUNCTION fn_zone_summary()
           RETURNS TABLE(
               zone_name      VARCHAR,
               divisions      BIGINT,
               stations       BIGINT,
               staff_count    BIGINT,
               total_payroll  NUMERIC)
           LANGUAGE plpgsql AS $$
           BEGIN
               RETURN QUERY
               SELECT z.zone_name,
                      COUNT(DISTINCT d.division_id)  AS divisions,
                      COUNT(DISTINCT st.station_id)  AS stations,
                      COUNT(DISTINCT sf.staff_id)    AS staff_count,
                      COALESCE(SUM(sf.salary), 0)    AS total_payroll
               FROM zone z
               LEFT JOIN division d  ON z.zone_id = d.zone_id
               LEFT JOIN station  st ON d.division_id = st.division_id
               LEFT JOIN staff    sf ON z.zone_id = sf.zone_id
               GROUP BY z.zone_name
               ORDER BY staff_count DESC;
           END; $$)",

        nullptr
    };

    // ---------- TRIGGERS ----------
    const char* triggers[] = {
        // Validate schedule: src != dst and ending_ts after starting_ts
        R"(CREATE OR REPLACE FUNCTION fn_validate_schedule()
           RETURNS TRIGGER LANGUAGE plpgsql AS $$
           BEGIN
               IF NEW.source_station_id = NEW.destination_station_id THEN
                   RAISE EXCEPTION 'Source and destination stations cannot be the same';
               END IF;
               IF NEW.ending_ts IS NOT NULL AND NEW.ending_ts <= NEW.starting_ts THEN
                   RAISE EXCEPTION 'Ending timestamp must be after starting timestamp';
               END IF;
               RETURN NEW;
           END; $$)",

        R"(DROP TRIGGER IF EXISTS trg_validate_schedule ON schedule)",
        R"(CREATE TRIGGER trg_validate_schedule
           BEFORE INSERT OR UPDATE ON schedule
           FOR EACH ROW EXECUTE FUNCTION fn_validate_schedule())",

        // Auto-maintain total_coaches count on coach INSERT/DELETE
        R"(CREATE OR REPLACE FUNCTION fn_sync_coach_count()
           RETURNS TRIGGER LANGUAGE plpgsql AS $$
           BEGIN
               IF TG_OP = 'INSERT' THEN
                   UPDATE train SET total_coaches = total_coaches + 1
                   WHERE train_id = NEW.train_id;
               ELSIF TG_OP = 'DELETE' THEN
                   UPDATE train SET total_coaches = GREATEST(total_coaches - 1, 0)
                   WHERE train_id = OLD.train_id;
               END IF;
               RETURN NEW;
           END; $$)",

        R"(DROP TRIGGER IF EXISTS trg_sync_coach_count ON coach)",
        R"(CREATE TRIGGER trg_sync_coach_count
           AFTER INSERT OR DELETE ON coach
           FOR EACH ROW EXECUTE FUNCTION fn_sync_coach_count())",

        nullptr
    };

    int ok = 0, fail = 0;
    auto run = [&](const char** arr) {
        for (int i = 0; arr[i]; i++) {
            PGresult* r = PQexec(conn, arr[i]);
            (PQresultStatus(r) == PGRES_COMMAND_OK) ? ok++ : fail++;
            PQclear(r);
        }
    };
    run(views); run(funcs); run(triggers);

    cout << "  DB objects initialized: "
         << ok << " OK  |  " << fail << " skipped/failed\n";
}

// ============================================================
//  MODULE 1  — USER MANAGEMENT
// ============================================================

void viewUsers() {
    cout << "\n=== REGISTERED USERS ===\n";
    qshow("SELECT user_id, name, email, dob, phone FROM registered_user ORDER BY name");
}

void registerUser() {
    cout << "\n=== REGISTER NEW USER ===\n";
    string uid  = genID("USR");
    string name = getInput("  Full Name   : ");
    string email= getInput("  Email       : ");
    string phone= getInput("  Phone       : ");
    string dob  = getInput("  DOB (YYYY-MM-DD): ");
    string addr = getInput("  Address     : ");
    string pwd  = getInput("  Password    : ");

    // duplicate email check
    if (!fetchOne("SELECT 1 FROM registered_user WHERE email='" + esc(email) + "'").empty()) {
        cout << "  [!] Email already registered.\n"; return;
    }
    if (!fetchOne("SELECT 1 FROM registered_user WHERE phone='" + esc(phone) + "'").empty()) {
        cout << "  [!] Phone already registered.\n"; return;
    }

    string sql = "INSERT INTO registered_user VALUES('"
        + uid + "','" + esc(name) + "','" + esc(email) + "','"
        + esc(pwd) + "','" + dob + "','" + esc(addr) + "','" + phone + "')";
    if (execSQL(sql)) cout << "  New User ID: " << uid << "\n";
}

void searchUser() {
    string q = getInput("  Search (name/email): ");
    qshow("SELECT user_id, name, email, dob, phone FROM registered_user "
          "WHERE LOWER(name) LIKE LOWER('%" + esc(q) + "%') "
          "   OR LOWER(email) LIKE LOWER('%" + esc(q) + "%')");
}

void deleteUser() {
    viewUsers();
    string uid = getInput("  User ID to delete: ");
    if (getInput("  Confirm? (yes/no): ") == "yes")
        execSQL("DELETE FROM registered_user WHERE user_id='" + esc(uid) + "'");
}

void userMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║     USER MANAGEMENT         ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Users          ║\n"
             << "║  2. Register New User       ║\n"
             << "║  3. Search User             ║\n"
             << "║  4. Delete User             ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewUsers();    break;
            case 2: registerUser(); break;
            case 3: searchUser();   break;
            case 4: deleteUser();   break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 2  — ORGANISATION  (Zone / Division / Department)
// ============================================================

void viewZones() {
    cout << "\n=== ZONES ===\n";
    qshow("SELECT * FROM zone ORDER BY zone_name");
}
void viewDivisions() {
    cout << "\n=== DIVISIONS ===\n";
    qshow("SELECT d.division_id, d.division_name, d.division_headquarters, z.zone_name "
          "FROM division d JOIN zone z ON d.zone_id = z.zone_id ORDER BY z.zone_name, d.division_name");
}
void viewDepts() {
    cout << "\n=== DEPARTMENTS ===\n";
    qshow("SELECT * FROM department ORDER BY dept_name");
}

void addZone() {
    string id = getInput("  Zone ID  : ");
    string nm = getInput("  Zone Name: ");
    string hq = getInput("  HQ City  : ");
    execSQL("INSERT INTO zone VALUES('" + esc(id) + "','" + esc(nm) + "','" + esc(hq) + "')");
}
void addDivision() {
    viewZones();
    string id  = getInput("  Division ID  : ");
    string zid = getInput("  Zone ID      : ");
    string nm  = getInput("  Division Name: ");
    string hq  = getInput("  HQ City      : ");
    execSQL("INSERT INTO division VALUES('" + esc(id) + "','" + esc(zid) + "','" + esc(nm) + "','" + esc(hq) + "')");
}
void addDept() {
    string id   = getInput("  Dept ID         : ");
    string nm   = getInput("  Dept Name       : ");
    string desc = getInput("  Description     : ");
    string head = getInput("  Head Officer    : ");
    execSQL("INSERT INTO department VALUES('" + esc(id) + "','" + esc(nm) + "','" + esc(desc) + "','" + esc(head) + "')");
}

void orgMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║   ORGANISATION MANAGEMENT   ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View Zones              ║\n"
             << "║  2. Add Zone                ║\n"
             << "║  3. View Divisions          ║\n"
             << "║  4. Add Division            ║\n"
             << "║  5. View Departments        ║\n"
             << "║  6. Add Department          ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewZones();     break;
            case 2: addZone();       break;
            case 3: viewDivisions(); break;
            case 4: addDivision();   break;
            case 5: viewDepts();     break;
            case 6: addDept();       break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 3  — STATION MANAGEMENT
// ============================================================

void viewStations() {
    cout << "\n=== STATIONS ===\n";
    qshow("SELECT s.station_id, s.station_name, s.category, s.city, s.state, "
          "       s.is_junction, d.division_name "
          "FROM station s JOIN division d ON s.division_id = d.division_id "
          "ORDER BY s.state, s.city");
}

void addStation() {
    viewDivisions();
    string id   = getInput("  Station ID  : ");
    string divid= getInput("  Division ID : ");
    string nm   = getInput("  Station Name: ");
    string cat  = getInput("  Category (A/B/C/Junction): ");
    string state= getInput("  State : ");
    string city = getInput("  City  : ");
    string junc = getInput("  Is Junction? (true/false): ");
    execSQL("INSERT INTO station(station_id,division_id,station_name,category,state,city,is_junction) VALUES('"
            + esc(id) + "','" + esc(divid) + "','" + esc(nm) + "','"
            + esc(cat) + "','" + esc(state) + "','" + esc(city) + "'," + junc + ")");
}

void searchStation() {
    string q = getInput("  Search (name/city/state): ");
    qshow("SELECT station_id, station_name, category, city, state, is_junction "
          "FROM station WHERE LOWER(station_name) LIKE LOWER('%" + esc(q) + "%') "
          "   OR LOWER(city)  LIKE LOWER('%" + esc(q) + "%') "
          "   OR LOWER(state) LIKE LOWER('%" + esc(q) + "%')");
}

void updateStationCat() {
    viewStations();
    string sid = getInput("  Station ID   : ");
    string cat = getInput("  New Category : ");
    execSQL("UPDATE station SET category='" + esc(cat) + "' WHERE station_id='" + esc(sid) + "'");
}

void stationMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║    STATION MANAGEMENT       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Stations       ║\n"
             << "║  2. Add Station             ║\n"
             << "║  3. Search Station          ║\n"
             << "║  4. Update Station Category ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewStations();    break;
            case 2: addStation();      break;
            case 3: searchStation();   break;
            case 4: updateStationCat();break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 4  — ROUTE MANAGEMENT
// ============================================================

void viewRoutes() {
    cout << "\n=== ROUTES ===\n";
    qshow("SELECT route_id, route_name, total_distance FROM route ORDER BY route_name");
}

void viewRouteDetail() {
    viewRoutes();
    string rid = getInput("  Route ID: ");
    qshow("SELECT visiting_order, station_id, station_name, city, platform_no, "
          "       arrival, departure, distance_from_source "
          "FROM vw_route_stations WHERE route_id='" + esc(rid) + "'");
}

void addRoute() {
    string id   = getInput("  Route ID  : ");
    string nm   = getInput("  Route Name: ");
    double dist = getDblInput("  Distance (km): ");
    execSQL("INSERT INTO route VALUES('" + esc(id) + "','" + esc(nm) + "'," + to_string(dist) + ")");
}

void addStationToRoute() {
    viewRoutes();  string rid = getInput("  Route ID   : ");
    viewStations();string sid = getInput("  Station ID : ");
    int    ord  = getIntInput("  Visiting Order: ");
    string plat = getInput("  Platform No : ");
    string arr  = getInput("  Arrival  Time (HH:MM:SS, blank if first): ");
    string dep  = getInput("  Departure Time (HH:MM:SS): ");
    double dist = getDblInput("  Distance from Source (km): ");
    string arrV = arr.empty() ? "NULL" : "'" + arr + "'";
    execSQL("INSERT INTO route_station(route_id,station_id,platform_no,visiting_order,"
            "arrival_time,departure_time,distance_from_source) VALUES('"
            + esc(rid) + "','" + esc(sid) + "','" + esc(plat) + "',"
            + to_string(ord) + "," + arrV + ",'" + dep + "'," + to_string(dist) + ")");
}

void routeMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║      ROUTE MANAGEMENT       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Routes         ║\n"
             << "║  2. View Route Detail       ║\n"
             << "║  3. Add Route               ║\n"
             << "║  4. Add Station to Route    ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewRoutes();         break;
            case 2: viewRouteDetail();    break;
            case 3: addRoute();           break;
            case 4: addStationToRoute();  break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 5  — TRAIN MANAGEMENT
// ============================================================

void viewTrains() {
    cout << "\n=== TRAINS ===\n";
    qshow("SELECT * FROM vw_train_details ORDER BY train_name");
}

void addTrain() {
    cout << "\n--- Add New Train ---\n";
    viewRoutes();  string rid  = getInput("  Route ID  : ");
    string cls_q  = "SELECT class_id, class_name, fare_multiplier FROM train_class";
    qshow(cls_q);  string clsid= getInput("  Class ID  : ");
    qshow("SELECT loco_id, loco_class, status FROM locomotive WHERE status='Active'");
    string lid  = getInput("  Loco ID (blank=unassigned): ");
    string id   = getInput("  Train ID  : ");
    string nm   = getInput("  Train Name: ");
    string tp   = getInput("  Train Type (Express/Mail/Passenger/Rajdhani/Shatabdi): ");
    double fare = getDblInput("  Base Fare (INR): ");
    string locoV = lid.empty() ? "NULL" : "'" + esc(lid) + "'";
    string sql = "INSERT INTO train(train_id,route_id,loco_id,class_id,train_name,train_type,base_fare) "
                 "VALUES('" + esc(id) + "','" + esc(rid) + "'," + locoV + ",'" + esc(clsid)
               + "','" + esc(nm) + "','" + esc(tp) + "'," + to_string(fare) + ")";
    if (execSQL(sql)) cout << "  Train ID: " << id << "\n";
}

void updateTrainStatus() {
    viewTrains();
    string tid = getInput("  Train ID: ");
    cout << "  Status options: Active  Maintenance  Retired  Cancelled\n";
    string st = getInput("  New Status: ");
    execSQL("UPDATE train SET train_status='" + esc(st) + "' WHERE train_id='" + esc(tid) + "'");
}

void addCoach() {
    cout << "\n--- Add Coach to Train ---\n";
    qshow("SELECT train_id, train_name FROM train WHERE train_status='Active'");
    string tid  = getInput("  Train ID     : ");
    string code = getInput("  Coach Code   : ");
    string type = getInput("  Coach Type   : ");
    double mult = getDblInput("  Fare Multiplier: ");
    int    seats= getIntInput("  Total Seats  : ");
    string ac   = getInput("  AC? (true/false): ");

    if (execSQL("INSERT INTO coach VALUES('" + esc(tid) + "','" + esc(code) + "','"
                + esc(type) + "'," + to_string(mult) + "," + to_string(seats) + "," + ac + ")")) {
        // Auto-populate seat table
        int added = 0;
        static const char* berths[] = {"Lower","Middle","Upper","Side Lower","Side Upper",
                                        "Lower","Middle","Upper"};
        for (int i = 1; i <= seats; i++) {
            string snum = to_string(i);
            string berth = berths[(i-1) % 8];
            string ss = "INSERT INTO seat VALUES('" + esc(tid) + "','" + esc(code) + "','"
                      + snum + "','" + berth + "')";
            PGresult* r = PQexec(conn, ss.c_str());
            if (PQresultStatus(r) == PGRES_COMMAND_OK) added++;
            PQclear(r);
        }
        cout << "  ✓ Added " << added << " seats to coach " << code << "\n";
    }
}

void viewCoaches() {
    qshow("SELECT train_id, train_name FROM train");
    string tid = getInput("  Train ID: ");
    qshow("SELECT coach_code, coach_type, fare_multiplier, total_seats, ac_flag "
          "FROM coach WHERE train_id='" + esc(tid) + "' ORDER BY coach_code");
}

void addTrainClass() {
    qshow("SELECT * FROM train_class");
    string id  = getInput("  Class ID  : ");
    string nm  = getInput("  Class Name: ");
    double mul = getDblInput("  Fare Multiplier: ");
    execSQL("INSERT INTO train_class VALUES('" + esc(id) + "','" + esc(nm) + "'," + to_string(mul) + ")");
}

void addLoco() {
    string id  = getInput("  Loco ID    : ");
    string cls = getInput("  Loco Class : ");
    string st  = getInput("  Status (Active/Maintenance/Retired): ");
    execSQL("INSERT INTO locomotive(loco_id,loco_class,status) VALUES('"
            + esc(id) + "','" + esc(cls) + "','" + esc(st) + "')");
}

void trainMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║      TRAIN MANAGEMENT       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Trains         ║\n"
             << "║  2. Add New Train           ║\n"
             << "║  3. Update Train Status     ║\n"
             << "║  4. Add Coach to Train      ║\n"
             << "║  5. View Train Coaches      ║\n"
             << "║  6. Add Train Class         ║\n"
             << "║  7. Add Locomotive          ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewTrains();        break;
            case 2: addTrain();          break;
            case 3: updateTrainStatus(); break;
            case 4: addCoach();          break;
            case 5: viewCoaches();       break;
            case 6: addTrainClass();     break;
            case 7: addLoco();           break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 6  — SCHEDULE MANAGEMENT
// ============================================================

void viewSchedules() {
    cout << "\n=== SCHEDULES ===\n";
    qshow("SELECT * FROM vw_active_schedules ORDER BY departure");
}

void addSchedule() {
    cout << "\n--- Add Schedule ---\n";
    qshow("SELECT train_id, train_name, train_type FROM train WHERE train_status='Active'");
    string tid = getInput("  Train ID : ");

    string rid = fetchOne("SELECT route_id FROM train WHERE train_id='" + esc(tid) + "'");
    if (rid.empty()) { cout << "  Train not found.\n"; return; }

    string dep = getInput("  Departure (YYYY-MM-DD HH:MM:SS): ");

    qshow("SELECT visiting_order, station_id, station_name FROM vw_route_stations "
          "WHERE route_id='" + esc(rid) + "'");
    string src = getInput("  Source Station ID     : ");
    string dst = getInput("  Destination Station ID: ");

    execSQL("INSERT INTO schedule(train_id,starting_ts,route_id,source_station_id,"
            "destination_station_id,status) VALUES('"
            + esc(tid) + "','" + dep + "','" + esc(rid) + "','"
            + esc(src) + "','" + esc(dst) + "','Scheduled')");
}

void updateScheduleStatus() {
    viewSchedules();
    string tid = getInput("  Train ID : ");
    string ts  = getInput("  Starting Timestamp: ");
    cout << "  Status options: Scheduled  Running  Completed  Cancelled  Delayed\n";
    string st  = getInput("  New Status: ");
    execSQL("UPDATE schedule SET status='" + esc(st) + "' "
            "WHERE train_id='" + esc(tid) + "' AND starting_ts='" + ts + "'");
}

void deleteSchedule() {
    viewSchedules();
    string tid = getInput("  Train ID : ");
    string ts  = getInput("  Starting Timestamp: ");
    if (getInput("  Confirm delete? (yes/no): ") == "yes")
        execSQL("DELETE FROM schedule WHERE train_id='" + esc(tid) + "' AND starting_ts='" + ts + "'");
}

void scheduleMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║   SCHEDULE MANAGEMENT       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Schedules      ║\n"
             << "║  2. Add New Schedule        ║\n"
             << "║  3. Update Schedule Status  ║\n"
             << "║  4. Delete Schedule         ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewSchedules();       break;
            case 2: addSchedule();         break;
            case 3: updateScheduleStatus();break;
            case 4: deleteSchedule();      break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 7  — TICKET BOOKING & MANAGEMENT
// ============================================================

void viewTickets() {
    cout << "\n=== TICKETS (last 50) ===\n";
    qshow("SELECT * FROM vw_ticket_details ORDER BY booked_at DESC LIMIT 50");
}

void searchTicket() {
    string pnr = getInput("  PNR: ");
    qshow("SELECT * FROM vw_ticket_details WHERE pnr='" + esc(pnr) + "'");
    cout << "\n--- Passengers ---\n";
    qshow("SELECT passenger_id, name, gender, dob, berth_pref, coach_code, seat_number "
          "FROM passenger WHERE pnr='" + esc(pnr) + "'");
}

void bookTicket() {
    cout << "\n=== BOOK TICKET ===\n";

    // 1. Select user
    viewUsers();
    string uid = getInput("  User ID: ");
    if (fetchOne("SELECT 1 FROM registered_user WHERE user_id='" + esc(uid) + "'").empty()) {
        cout << "  [!] User not found.\n"; return;
    }

    // 2. Select schedule
    qshow("SELECT * FROM vw_active_schedules WHERE status='Scheduled' ORDER BY departure");
    string tid = getInput("  Train ID : ");
    string ts  = getInput("  Schedule Starting Timestamp: ");

    string rid = fetchOne("SELECT route_id FROM train WHERE train_id='" + esc(tid) + "'");
    if (rid.empty()) { cout << "  Train not found.\n"; return; }

    // 3. Station selection
    qshow("SELECT visiting_order, station_id, station_name FROM vw_route_stations "
          "WHERE route_id='" + esc(rid) + "'");
    string boarding = getInput("  Boarding Station ID    : ");
    string dest     = getInput("  Destination Station ID : ");

    // 4. Coach / fare info
    qshow("SELECT coach_code, coach_type, fare_multiplier, total_seats FROM coach WHERE train_id='" + esc(tid) + "'");

    int    count  = getIntInput("  No. of Passengers: ");
    double baseFare = 0;
    string bf = fetchOne("SELECT base_fare FROM train WHERE train_id='" + esc(tid) + "'");
    if (!bf.empty()) baseFare = stod(bf);

    double total = baseFare * count;
    cout << "  Estimated Total: INR " << fixed << setprecision(2) << total
         << "  (INR " << baseFare << " x " << count << ")\n";
    string method = getInput("  Payment Method (UPI/Credit Card/Debit Card/Net Banking/Cash): ");

    // 5. Generate IDs
    string nowStr = nowTS();
    // strip non-alnum
    string raw = nowStr;
    raw.erase(remove_if(raw.begin(), raw.end(), [](char c){return !isalnum(c);}), raw.end());
    string txnId   = "TXN" + raw;
    string pnr     = "PNR" + raw.substr(0, 14);

    // 6. Begin transaction
    PQexec(conn, "BEGIN");

    // 7. Payment
    if (!execSQL("INSERT INTO payment(transaction_id,amount,method,payment_time) VALUES('"
                 + txnId + "'," + to_string(total) + ",'" + esc(method) + "','" + nowStr + "')")) {
        PQexec(conn, "ROLLBACK"); return;
    }

    // 8. Ticket
    if (!execSQL("INSERT INTO tickets(pnr,user_id,transaction_id,boarding_station,"
                 "destination_station,schedule_train_id,schedule_starting_ts,passenger_count,booking_ts) "
                 "VALUES('" + pnr + "','" + esc(uid) + "','" + txnId + "','"
                 + esc(boarding) + "','" + esc(dest) + "','" + esc(tid) + "','"
                 + ts + "'," + to_string(count) + ",'" + nowStr + "')")) {
        PQexec(conn, "ROLLBACK"); return;
    }

    // 9. Fetch available seats
    PGresult* seatRes = PQexec(conn,
        ("SELECT se.train_id, se.coach_code, se.seat_number, se.berth_type "
         "FROM seat se WHERE se.train_id='" + esc(tid) + "' "
         "AND NOT EXISTS ("
         "  SELECT 1 FROM passenger p2 "
         "  WHERE p2.train_id=se.train_id AND p2.coach_code=se.coach_code "
         "  AND p2.seat_number=se.seat_number "
         "  AND p2.pnr IN (SELECT pnr FROM tickets "
         "                 WHERE schedule_train_id='" + esc(tid) + "' "
         "                   AND schedule_starting_ts='" + ts + "'))"
         " LIMIT " + to_string(count)).c_str());

    int availSeats = PQntuples(seatRes);

    // 10. Add passengers
    for (int i = 0; i < count; i++) {
        cout << "\n  --- Passenger " << (i+1) << " of " << count << " ---\n";
        string pname  = getInput("  Name  : ");
        string gender = getInput("  Gender: ");
        string pdob   = getInput("  DOB (YYYY-MM-DD): ");
        string bpref  = getInput("  Berth Pref (Lower/Middle/Upper/Any): ");

        string pid = "P" + to_string(i+1) + raw.substr(0, 10);
        string cc, snum, btype;
        if (i < availSeats) {
            cc    = PQgetvalue(seatRes, i, 1);
            snum  = PQgetvalue(seatRes, i, 2);
            btype = PQgetvalue(seatRes, i, 3);
        } else {
            cc = "WL"; snum = "WL-" + to_string(i+1); btype = "Waitlist";
        }

        execSQL("INSERT INTO passenger(passenger_id,pnr,train_id,coach_code,seat_number,"
                "name,gender,dob,berth_pref) VALUES('"
                + pid + "','" + pnr + "','" + esc(tid) + "','" + esc(cc) + "','"
                + esc(snum) + "','" + esc(pname) + "','" + esc(gender) + "','"
                + pdob + "','" + esc(bpref) + "')");
    }
    PQclear(seatRes);
    PQexec(conn, "COMMIT");

    cout << "\n\033[1;32m"
         << "╔══════════════════════════════════════════╗\n"
         << "║         BOOKING CONFIRMED ✓              ║\n"
         << "╠══════════════════════════════════════════╣\n"
         << "║  PNR    : " << pnr << "\n"
         << "║  Amount : INR " << fixed << setprecision(2) << total << "\n"
         << "║  Method : " << method << "\n"
         << "╚══════════════════════════════════════════╝\033[0m\n";
}

void cancelTicket() {
    string pnr = getInput("  PNR: ");
    searchTicket();
    if (getInput("  Confirm cancellation? (yes/no): ") != "yes") {
        cout << "  Cancelled.\n"; return;
    }
    string reason   = getInput("  Reason      : ");
    double refundPct= getDblInput("  Refund %     : ");

    // Call stored function
    string sql = "SELECT fn_cancel_ticket('" + esc(pnr) + "','"
               + esc(reason) + "'," + to_string(refundPct) + ")";
    PGresult* r = PQexec(conn, sql.c_str());
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        cout << "  " << PQgetvalue(r, 0, 0) << "\n";
    else
        cout << "  Error: " << PQresultErrorMessage(r) << "\n";
    PQclear(r);
}

void viewRefunds() {
    cout << "\n=== REFUND RECORDS ===\n";
    qshow("SELECT tr.refund_id, tr.pnr, u.name, tr.reason_code, "
          "       tr.refund_amount, tr.refund_date::text "
          "FROM ticket_refund tr "
          "JOIN tickets tk ON tr.pnr = tk.pnr "
          "JOIN registered_user u ON tk.user_id = u.user_id "
          "ORDER BY tr.refund_date DESC");
}

void ticketMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════════╗\n"
             << "║     TICKET MANAGEMENT           ║\n"
             << "╠══════════════════════════════════╣\n"
             << "║  1. View All Tickets            ║\n"
             << "║  2. Search by PNR               ║\n"
             << "║  3. Book Ticket                 ║\n"
             << "║  4. Cancel Ticket / Refund      ║\n"
             << "║  5. View Refund Records         ║\n"
             << "║  0. Back                        ║\n"
             << "╚══════════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewTickets();  break;
            case 2: searchTicket(); break;
            case 3: bookTicket();   break;
            case 4: cancelTicket(); break;
            case 5: viewRefunds();  break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 8  — STAFF MANAGEMENT
// ============================================================

void viewStaff() {
    cout << "\n=== STAFF ===\n";
    qshow("SELECT * FROM vw_staff_details ORDER BY dept_name, name");
}

void addStaff() {
    cout << "\n--- Add Staff Member ---\n";
    viewZones();     string zid   = getInput("  Zone ID  : ");
    viewDivisions(); string divid = getInput("  Div ID   : ");
    viewDepts();     string deptid= getInput("  Dept ID  : ");

    string sid    = getInput("  Staff ID  : ");
    string nm     = getInput("  Name      : ");
    string role   = getInput("  Role      : ");
    string phone  = getInput("  Phone     : ");
    string email  = getInput("  Email     : ");
    double salary = getDblInput("  Salary  : ");
    string stid   = getInput("  Station ID (blank if N/A): ");
    string stnV   = stid.empty() ? "NULL" : "'" + esc(stid) + "'";

    if (!execSQL("INSERT INTO staff(staff_id,zone_id,division_id,station_id,dept_id,"
                 "name,role,phone,email,salary) VALUES('"
                 + esc(sid) + "','" + esc(zid) + "','" + esc(divid) + "',"
                 + stnV + ",'" + esc(deptid) + "','" + esc(nm) + "','"
                 + esc(role) + "','" + esc(phone) + "','" + esc(email) + "',"
                 + to_string(salary) + ")")) return;

    // Role-specific sub-tables
    string rlow = role; transform(rlow.begin(), rlow.end(), rlow.begin(), ::tolower);
    if (rlow == "driver") {
        qshow("SELECT train_id, train_name FROM train WHERE train_status='Active'");
        string tid  = getInput("  Assign Train ID : ");
        string lic  = getInput("  License No      : ");
        execSQL("INSERT INTO driver VALUES('" + esc(sid) + "','" + esc(tid) + "','" + esc(lic) + "')");
    } else if (rlow == "guard") {
        qshow("SELECT train_id, train_name FROM train WHERE train_status='Active'");
        string tid  = getInput("  Assign Train ID       : ");
        string clr  = getInput("  Security Clearance    : ");
        execSQL("INSERT INTO guard VALUES('" + esc(sid) + "','" + esc(tid) + "','" + esc(clr) + "')");
    } else if (rlow == "ticket checker" || rlow == "tc") {
        viewStations();
        string staid = getInput("  Assigned Station ID: ");
        execSQL("INSERT INTO ticket_checker(staff_id, station_id) VALUES('"
                + esc(sid) + "','" + esc(staid) + "')");
    }
    cout << "  Staff added: " << nm << " [" << sid << "]\n";
}

void updateSalary() {
    viewStaff();
    string sid = getInput("  Staff ID : ");
    double sal = getDblInput("  New Salary: ");
    execSQL("UPDATE staff SET salary=" + to_string(sal) + " WHERE staff_id='" + esc(sid) + "'");
}

void deleteStaff() {
    string sid = getInput("  Staff ID to delete: ");
    if (getInput("  Confirm? (yes/no): ") == "yes")
        execSQL("DELETE FROM staff WHERE staff_id='" + esc(sid) + "'");
}

void viewDrivers() {
    cout << "\n=== DRIVERS ===\n";
    qshow("SELECT s.staff_id, s.name, d.train_id, t.train_name, d.driving_license_no, s.salary "
          "FROM driver d JOIN staff s ON d.staff_id = s.staff_id "
          "JOIN train t ON d.train_id = t.train_id");
}

void viewGuards() {
    cout << "\n=== GUARDS ===\n";
    qshow("SELECT s.staff_id, s.name, g.train_id, t.train_name, g.security_clearance, s.salary "
          "FROM guard g JOIN staff s ON g.staff_id = s.staff_id "
          "JOIN train t ON g.train_id = t.train_id");
}

void staffMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║      STAFF MANAGEMENT       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Staff          ║\n"
             << "║  2. Add Staff Member        ║\n"
             << "║  3. Update Staff Salary     ║\n"
             << "║  4. Delete Staff            ║\n"
             << "║  5. View Drivers            ║\n"
             << "║  6. View Guards             ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewStaff();    break;
            case 2: addStaff();     break;
            case 3: updateSalary(); break;
            case 4: deleteStaff();  break;
            case 5: viewDrivers();  break;
            case 6: viewGuards();   break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 9  — MAINTENANCE MANAGEMENT
// ============================================================

void viewMaintenance() {
    cout << "\n=== MAINTENANCE RECORDS ===\n";
    qshow("SELECT * FROM vw_maintenance_summary ORDER BY maintenance_date DESC");
}

void addMaintenance() {
    cout << "\n--- Add Maintenance Record ---\n";
    qshow("SELECT train_id, train_name FROM train");
    string tid  = getInput("  Train ID        : ");
    string mid  = getInput("  Maintenance ID  : ");
    string dt   = getInput("  Date (YYYY-MM-DD, blank=today): ");
    string type = getInput("  Type (Routine/Emergency/Scheduled/Major Overhaul): ");
    string desc = getInput("  Description     : ");
    string stat = getInput("  Status (Pending/In Progress/Completed): ");
    string dtV  = dt.empty() ? "CURRENT_DATE" : "'" + dt + "'";

    if (!execSQL("INSERT INTO maintenance_record(maintenance_id,train_id,maintenance_date,"
                 "maintenance_type,description,maintenance_status) VALUES('"
                 + esc(mid) + "','" + esc(tid) + "'," + dtV + ",'"
                 + esc(type) + "','" + esc(desc) + "','" + esc(stat) + "')")) return;

    while (getInput("  Assign staff to this record? (yes/no): ") == "yes") {
        qshow("SELECT staff_id, name, role FROM staff LIMIT 20");
        string sfid = getInput("  Staff ID: ");
        execSQL("INSERT INTO maintenance_staff VALUES('" + esc(sfid) + "','" + esc(mid) + "')");
    }
}

void updateMaintenanceStat() {
    viewMaintenance();
    string mid  = getInput("  Maintenance ID: ");
    string stat = getInput("  New Status (Pending/In Progress/Completed): ");
    execSQL("UPDATE maintenance_record SET maintenance_status='" + esc(stat) + "' "
            "WHERE maintenance_id='" + esc(mid) + "'");
}

void maintenanceMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║   MAINTENANCE MANAGEMENT    ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View Records            ║\n"
             << "║  2. Add Record              ║\n"
             << "║  3. Update Status           ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewMaintenance();      break;
            case 2: addMaintenance();       break;
            case 3: updateMaintenanceStat();break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 10 — LIVE TRAIN STATUS
// ============================================================

void viewLive() {
    cout << "\n=== LIVE TRAIN STATUS ===\n";
    qshow("SELECT * FROM vw_live_status ORDER BY scheduled_dep DESC");
}

void updateLive() {
    viewSchedules();
    string tid  = getInput("  Train ID  : ");
    string ts   = getInput("  Schedule TS: ");
    viewStations();
    string sid  = getInput("  Current Station ID: ");
    int    delay= getIntInput("  Delay (minutes, 0=on time): ");
    string reason = delay > 0 ? getInput("  Delay Reason: ") : "";
    cout << "  Status: On Time | Delayed | Arrived | Departed | Running\n";
    string status = getInput("  Status: ");

    bool exists = !fetchOne("SELECT 1 FROM live_train_status "
                             "WHERE schedule_train_id='" + esc(tid) + "' "
                             "AND schedule_starting_ts='" + ts + "' "
                             "AND station_id='" + esc(sid) + "'").empty();
    if (exists)
        execSQL("UPDATE live_train_status SET delay_minutes=" + to_string(delay)
                + ",delay_reason='" + esc(reason) + "',status='" + esc(status)
                + "',reported_time=CURRENT_TIMESTAMP "
                "WHERE schedule_train_id='" + esc(tid) + "' "
                "AND schedule_starting_ts='" + ts + "' "
                "AND station_id='" + esc(sid) + "'");
    else
        execSQL("INSERT INTO live_train_status(schedule_train_id,schedule_starting_ts,"
                "station_id,delay_minutes,delay_reason,status,reported_time) VALUES('"
                + esc(tid) + "','" + ts + "','" + esc(sid) + "',"
                + to_string(delay) + ",'" + esc(reason) + "','" + esc(status)
                + "',CURRENT_TIMESTAMP)");
}

void searchLive() {
    string q = getInput("  Train ID or Name: ");
    qshow("SELECT train_name, scheduled_dep, station_name, city, delay_minutes, reason, status, last_updated "
          "FROM vw_live_status WHERE train_id='" + esc(q) + "' "
          "   OR LOWER(train_name) LIKE LOWER('%" + esc(q) + "%') "
          "ORDER BY last_updated DESC");
}

void liveMenu() {
    while (true) {
        cout << "\n\033[1;36m╔══════════════════════════════╗\n"
             << "║     LIVE TRAIN STATUS       ║\n"
             << "╠══════════════════════════════╣\n"
             << "║  1. View All Live Status    ║\n"
             << "║  2. Update Train Status     ║\n"
             << "║  3. Search Train Status     ║\n"
             << "║  0. Back                    ║\n"
             << "╚══════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: viewLive();   break;
            case 2: updateLive(); break;
            case 3: searchLive(); break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MODULE 11 — REPORTS & ANALYTICS  (Stored Functions)
// ============================================================

void reportRevenue() {
    cout << "\n=== REVENUE BY TRAIN  [fn_revenue_by_train()] ===\n";
    qshow("SELECT * FROM fn_revenue_by_train()");
}

void reportSalary() {
    cout << "\n=== SALARY STATS BY DEPT  [fn_salary_stats()] ===\n";
    qshow("SELECT * FROM fn_salary_stats()");
}

void reportOccupancy() {
    cout << "\n=== TRAIN OCCUPANCY  [fn_get_train_occupancy()] ===\n";
    viewSchedules();
    string tid = getInput("  Train ID          : ");
    string ts  = getInput("  Schedule Timestamp: ");
    qshow("SELECT * FROM fn_get_train_occupancy('" + esc(tid) + "','" + ts + "')");
}

void reportZone() {
    cout << "\n=== ZONE SUMMARY  [fn_zone_summary()] ===\n";
    qshow("SELECT * FROM fn_zone_summary()");
}

void reportDelayed() {
    cout << "\n=== CURRENTLY DELAYED TRAINS ===\n";
    qshow("SELECT train_name, scheduled_dep, station_name, city, delay_minutes, reason, last_updated "
          "FROM vw_live_status WHERE delay_minutes > 0 ORDER BY delay_minutes DESC");
}

void reportRoute() {
    cout << "\n=== ROUTE UTILISATION ===\n";
    qshow("SELECT r.route_id, r.route_name, r.total_distance, "
          "       COUNT(DISTINCT t.train_id)  AS trains, "
          "       COUNT(DISTINCT tk.pnr)      AS total_bookings "
          "FROM route r "
          "LEFT JOIN train     t  ON r.route_id              = t.route_id "
          "LEFT JOIN schedule  s  ON t.train_id              = s.train_id "
          "LEFT JOIN tickets   tk ON s.train_id              = tk.schedule_train_id "
          "GROUP BY r.route_id, r.route_name, r.total_distance "
          "ORDER BY total_bookings DESC");
}

void reportsMenu() {
    while (true) {
        cout << "\n\033[1;36m╔════════════════════════════════════╗\n"
             << "║       REPORTS & ANALYTICS        ║\n"
             << "╠════════════════════════════════════╣\n"
             << "║  1. Revenue by Train             ║\n"
             << "║  2. Salary Stats by Department   ║\n"
             << "║  3. Train Occupancy Report       ║\n"
             << "║  4. Zone Summary                 ║\n"
             << "║  5. Currently Delayed Trains     ║\n"
             << "║  6. Route Utilisation            ║\n"
             << "║  0. Back                         ║\n"
             << "╚════════════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Choice: ")) {
            case 1: reportRevenue();   break;
            case 2: reportSalary();    break;
            case 3: reportOccupancy(); break;
            case 4: reportZone();      break;
            case 5: reportDelayed();   break;
            case 6: reportRoute();     break;
            case 0: return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  MAIN MENU
// ============================================================

void mainMenu() {
    while (true) {
        cout << "\n\033[1;33m"
             << "╔═══════════════════════════════════════════════════╗\n"
             << "║   INDIAN RAILWAYS DATABASE MANAGEMENT SYSTEM     ║\n"
             << "║                 IT214 — DB Project 2026          ║\n"
             << "╠═══════════════════════════════════════════════════╣\n"
             << "║   1.  User Management                            ║\n"
             << "║   2.  Organisation  (Zone / Division / Dept)     ║\n"
             << "║   3.  Station Management                         ║\n"
             << "║   4.  Route Management                           ║\n"
             << "║   5.  Train Management                           ║\n"
             << "║   6.  Schedule Management                        ║\n"
             << "║   7.  Ticket Booking & Management                ║\n"
             << "║   8.  Staff Management                           ║\n"
             << "║   9.  Maintenance Management                     ║\n"
             << "║  10.  Live Train Status                          ║\n"
             << "║  11.  Reports & Analytics (Stored Procedures)    ║\n"
             << "║   0.  Exit                                       ║\n"
             << "╚═══════════════════════════════════════════════════╝\033[0m\n";
        switch (getIntInput("  Enter choice: ")) {
            case 1:  userMenu();        break;
            case 2:  orgMenu();         break;
            case 3:  stationMenu();     break;
            case 4:  routeMenu();       break;
            case 5:  trainMenu();       break;
            case 6:  scheduleMenu();    break;
            case 7:  ticketMenu();      break;
            case 8:  staffMenu();       break;
            case 9:  maintenanceMenu(); break;
            case 10: liveMenu();        break;
            case 11: reportsMenu();     break;
            case 0:
                cout << "\n  Goodbye! Thank you for using the Railway Management System.\n\n";
                return;
            default: cout << "  Invalid choice.\n";
        }
    }
}

// ============================================================
//  ENTRY POINT
// ============================================================

int main() {
    cout << "\033[1;34m"
         << "\n╔══════════════════════════════════════════════╗\n"
         << "║   RAILWAY MANAGEMENT SYSTEM — DB Connect     ║\n"
         << "╚══════════════════════════════════════════════╝\033[0m\n";

    string host = getInput("  Host     [localhost]: ");
    if (host.empty()) host = "localhost";
    string port = getInput("  Port     [5432]     : ");
    if (port.empty()) port = "5432";
    string dbname = getInput("  Database            : ");
    string user   = getInput("  Username            : ");
    string pwd    = getInput("  Password            : ");

    cout << "  Connecting...\n";
    if (!connectDB(host, port, dbname, user, pwd)) return 1;
    cout << "  \033[32m✓ Connected to PostgreSQL — schema: DB_Project\033[0m\n";

    cout << "  Setting up views & stored procedures...\n";
    setupDB();

    mainMenu();
    disconnectDB();
    return 0;
}
