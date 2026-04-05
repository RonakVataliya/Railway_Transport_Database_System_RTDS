CREATE SCHEMA IF NOT EXISTS DB_Project;

SET SEARCH_PATH TO DB_Project;


CREATE TABLE zone (
    zone_id VARCHAR(20) PRIMARY KEY,
    zone_name VARCHAR(100) NOT NULL,
    zone_headquarters VARCHAR(100) NOT NULL
);

CREATE TABLE division (
    division_id VARCHAR(20) PRIMARY KEY,
    zone_id VARCHAR(20) NOT NULL REFERENCES zone(zone_id) ON UPDATE CASCADE ON DELETE RESTRICT,
    division_name VARCHAR(100) NOT NULL,
    division_headquarters VARCHAR(100) NOT NULL
);

CREATE TABLE department (
    dept_id VARCHAR(20) PRIMARY KEY,
    dept_name VARCHAR(100) NOT NULL,
    description TEXT,
    head_officer_title VARCHAR(100) NOT NULL
);

CREATE TABLE station (
    station_id VARCHAR(20) PRIMARY KEY,
    division_id VARCHAR(20) NOT NULL REFERENCES division(division_id) ON UPDATE CASCADE ON DELETE RESTRICT,
    station_name VARCHAR(120) NOT NULL,
    category VARCHAR(50) NOT NULL,
    state VARCHAR(60) NOT NULL,
    city VARCHAR(60) NOT NULL,
    is_junction BOOLEAN DEFAULT FALSE
);

CREATE TABLE route (
    route_id VARCHAR(20) PRIMARY KEY,
    route_name VARCHAR(120) NOT NULL,
    total_distance NUMERIC(10,2) NOT NULL CHECK (total_distance >= 0)
);

CREATE TABLE train_class (
    class_id VARCHAR(20) PRIMARY KEY,
    class_name VARCHAR(60) NOT NULL,
    fare_multiplier NUMERIC(6,2) NOT NULL DEFAULT 1.0 CHECK (fare_multiplier > 0)
);


CREATE TABLE registered_user (
    user_id VARCHAR(20) PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(120) UNIQUE NOT NULL,
    password VARCHAR(200) NOT NULL,
    dob DATE NOT NULL,
    address TEXT NOT NULL,
    phone VARCHAR(20) UNIQUE NOT NULL
);

CREATE TABLE maintenance_shed (
    shed_id VARCHAR(20) PRIMARY KEY,
    division_id VARCHAR(20) NOT NULL REFERENCES division(division_id) ON UPDATE CASCADE ON DELETE RESTRICT,
    shed_name VARCHAR(100) NOT NULL,
    shed_type VARCHAR(50) NOT NULL,
    capacity INTEGER NOT NULL CHECK (capacity >= 0),
    inspection_flag BOOLEAN DEFAULT FALSE
);

CREATE TABLE locomotive (
    loco_id VARCHAR(20) PRIMARY KEY,
    loco_class VARCHAR(50) NOT NULL,
    status VARCHAR(40) NOT NULL DEFAULT 'Active',
    shed_id VARCHAR(20) REFERENCES maintenance_shed(shed_id) ON UPDATE CASCADE ON DELETE SET NULL
);

CREATE TABLE train (
    train_id VARCHAR(20) PRIMARY KEY,
    route_id VARCHAR(20) NOT NULL REFERENCES route(route_id) ON UPDATE CASCADE ON DELETE RESTRICT,
    loco_id VARCHAR(20) REFERENCES locomotive(loco_id) ON UPDATE CASCADE ON DELETE SET NULL,
    class_id VARCHAR(20) NOT NULL REFERENCES train_class(class_id) ON UPDATE CASCADE ON DELETE SET NULL,
    train_name VARCHAR(120) NOT NULL,
    train_type VARCHAR(60) NOT NULL,
    train_status VARCHAR(40) NOT NULL DEFAULT 'Active',
    total_coaches INTEGER NOT NULL DEFAULT 0 CHECK (total_coaches >= 0),
    base_fare NUMERIC(10,2) NOT NULL DEFAULT 0.00 CHECK (base_fare >= 0)
);


CREATE TABLE maintenance_record (
    maintenance_id VARCHAR(20) PRIMARY KEY,
    train_id VARCHAR(20) NOT NULL REFERENCES train(train_id) ON UPDATE CASCADE ON DELETE CASCADE,
    maintenance_date DATE NOT NULL DEFAULT CURRENT_DATE,
    maintenance_type VARCHAR(80) NOT NULL,
    description TEXT,
    maintenance_status VARCHAR(50) NOT NULL
);

CREATE TABLE staff (
    staff_id VARCHAR(20) PRIMARY KEY,
    zone_id VARCHAR(20) NOT NULL REFERENCES zone(zone_id),
    division_id VARCHAR(20) NOT NULL REFERENCES division(division_id),
    station_id VARCHAR(20) REFERENCES station(station_id),
    dept_id VARCHAR(20) NOT NULL REFERENCES department(dept_id),
    name VARCHAR(100) NOT NULL,
    role VARCHAR(80) NOT NULL,
    phone VARCHAR(20) UNIQUE NOT NULL,
    email VARCHAR(120) UNIQUE NOT NULL,
    salary NUMERIC(12,2) NOT NULL CHECK (salary >= 0)
);

CREATE TABLE maintenance_staff (
    staff_id VARCHAR(20) NOT NULL REFERENCES staff(staff_id) ON DELETE CASCADE,
    maintenance_id VARCHAR(20) NOT NULL REFERENCES maintenance_record(maintenance_id) ON DELETE CASCADE,
    PRIMARY KEY (staff_id, maintenance_id)
);

CREATE TABLE ticket_checker (
    staff_id VARCHAR(20) PRIMARY KEY REFERENCES staff(staff_id) ON DELETE CASCADE,
    station_id VARCHAR(20) NOT NULL REFERENCES station(station_id),
    fine_log NUMERIC(12,2) DEFAULT 0.00 CHECK (fine_log >= 0)
);

CREATE TABLE driver (
    staff_id VARCHAR(20) PRIMARY KEY REFERENCES staff(staff_id) ON DELETE CASCADE,
    train_id VARCHAR(20) NOT NULL REFERENCES train(train_id),
    driving_license_no VARCHAR(50) UNIQUE NOT NULL
);

CREATE TABLE guard (
    staff_id VARCHAR(20) PRIMARY KEY REFERENCES staff(staff_id) ON DELETE CASCADE,
    train_id VARCHAR(20) NOT NULL REFERENCES train(train_id),
    security_clearance VARCHAR(50) NOT NULL
);


CREATE TABLE route_station (
    route_id VARCHAR(20) NOT NULL REFERENCES route(route_id) ON DELETE CASCADE,
    station_id VARCHAR(20) NOT NULL REFERENCES station(station_id),
    platform_no VARCHAR(20),
    visiting_order INTEGER NOT NULL,
    arrival_time TIME,
    departure_time TIME,
    halt_time TIME DEFAULT '00:00:00',
    distance_from_source NUMERIC(10,2) NOT NULL CHECK (distance_from_source >= 0),
    PRIMARY KEY (route_id, station_id)
);

CREATE TABLE schedule (
    train_id VARCHAR(20) NOT NULL REFERENCES train(train_id) ON DELETE CASCADE,
    starting_ts TIMESTAMP NOT NULL,
    route_id VARCHAR(20) NOT NULL REFERENCES route(route_id),
    source_station_id VARCHAR(20) NOT NULL REFERENCES station(station_id),
    destination_station_id VARCHAR(20) NOT NULL REFERENCES station(station_id),
    running_time INTERVAL,
    status VARCHAR(40) NOT NULL DEFAULT 'Scheduled',
    ending_ts TIMESTAMP,
    PRIMARY KEY (train_id, starting_ts)
);


CREATE TABLE payment (
    transaction_id VARCHAR(80) PRIMARY KEY,
    amount NUMERIC(12,2) NOT NULL CHECK (amount >= 0),
    method VARCHAR(40) NOT NULL,
    payment_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tickets (
    pnr VARCHAR(20) PRIMARY KEY,
    user_id VARCHAR(20) NOT NULL REFERENCES registered_user(user_id),
    transaction_id VARCHAR(80) NOT NULL REFERENCES payment(transaction_id),
    boarding_station VARCHAR(20) NOT NULL REFERENCES station(station_id),
    destination_station VARCHAR(20) NOT NULL REFERENCES station(station_id),
    schedule_train_id VARCHAR(20) NOT NULL,
    schedule_starting_ts TIMESTAMP NOT NULL,
    passenger_count INTEGER NOT NULL CHECK (passenger_count > 0),
    booking_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_ticket_schedule FOREIGN KEY (schedule_train_id, schedule_starting_ts) 
        REFERENCES schedule(train_id, starting_ts)
);


CREATE TABLE coach (
    train_id VARCHAR(20) NOT NULL REFERENCES train(train_id) ON DELETE CASCADE,
    coach_code VARCHAR(20) NOT NULL,
    coach_type VARCHAR(60) NOT NULL,
    fare_multiplier NUMERIC(6,2) NOT NULL DEFAULT 1.0 CHECK (fare_multiplier > 0),
    total_seats INTEGER NOT NULL CHECK (total_seats >= 0),
    ac_flag BOOLEAN DEFAULT FALSE,
    PRIMARY KEY (train_id, coach_code)
);

CREATE TABLE seat (
    train_id VARCHAR(20) NOT NULL,
    coach_code VARCHAR(20) NOT NULL,
    seat_number VARCHAR(20) NOT NULL,
    berth_type VARCHAR(20) NOT NULL,
    PRIMARY KEY (train_id, coach_code, seat_number),
    CONSTRAINT fk_seat_coach FOREIGN KEY (train_id, coach_code) 
        REFERENCES coach(train_id, coach_code) ON DELETE CASCADE
);

CREATE TABLE passenger (
    passenger_id VARCHAR(20) NOT NULL,
    pnr VARCHAR(20) NOT NULL REFERENCES tickets(pnr) ON DELETE CASCADE,
    train_id VARCHAR(20) NOT NULL,
    coach_code VARCHAR(20) NOT NULL,
    seat_number VARCHAR(20) NOT NULL,
    name VARCHAR(100) NOT NULL,
    gender VARCHAR(20) NOT NULL,
    dob DATE NOT NULL,
    berth_pref VARCHAR(30),
    PRIMARY KEY (passenger_id, pnr),
    CONSTRAINT fk_passenger_seat FOREIGN KEY (train_id, coach_code, seat_number) 
        REFERENCES seat(train_id, coach_code, seat_number)
);


CREATE TABLE ticket_refund (
    refund_id VARCHAR(20) PRIMARY KEY,
    pnr VARCHAR(20) NOT NULL REFERENCES tickets(pnr) ON DELETE CASCADE,
    reason_code VARCHAR(40) NOT NULL,
    refund_amount NUMERIC(12,2) NOT NULL CHECK (refund_amount >= 0),
    refund_date DATE NOT NULL DEFAULT CURRENT_DATE
);

CREATE TABLE live_train_status (
    schedule_train_id VARCHAR(20) NOT NULL,
    schedule_starting_ts TIMESTAMP NOT NULL,
    station_id VARCHAR(20) NOT NULL REFERENCES station(station_id),
    delay_minutes INTEGER NOT NULL DEFAULT 0 CHECK (delay_minutes >= 0),
    delay_reason TEXT,
    reported_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(40) NOT NULL,
    PRIMARY KEY (schedule_train_id, schedule_starting_ts, station_id),
    CONSTRAINT fk_lts_schedule FOREIGN KEY (schedule_train_id, schedule_starting_ts) 
        REFERENCES schedule(train_id, starting_ts) ON DELETE CASCADE
);