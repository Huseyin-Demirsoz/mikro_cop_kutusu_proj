/*
 * Smart Trash Bin - C++ REST API + SQLite Server
 *
 * Features:
 *  - Receives ESP32 sensor data through POST /api/trash
 *  - Stores live/history/statistics data in SQLite
 *  - Serves web dashboard from public/index.html
 *  - Provides notification events for browser/mobile dashboard notifications
 *  - Optionally calls notify_hook.bat / notify_hook.sh for e-mail, Telegram, IFTTT, etc.
 *
 * Dependencies:
 *  - cpp-httplib: httplib.h in the same folder
 *  - SQLite C API: sqlite3.h + sqlite3.c or system sqlite3 library
 */

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "httplib.h"
#include "sqlite3.h"

std::mutex dbMutex;

const char* DB_NAME = "trash_data.db";
const int SERVER_PORT = 8080;

const int FILL_WARNING = 70;
const int FILL_ALARM = 90;
const int GAS_WARNING = 400;
const int GAS_ALARM = 800;

// Avoid duplicate external messages when the sensor sends frequent ALARM records.
const int NOTIFICATION_COOLDOWN_SECONDS = 300;

// =========================
// Utility helpers
// =========================
std::string jsonEscape(const std::string& s) {
    std::string out;

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }

    return out;
}

std::string columnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}


std::string shellQuote(const std::string& value) {
    std::string out = "\"";

    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\n' || c == '\r') {
            out += ' ';
        } else {
            out += c;
        }
    }

    out += "\"";
    return out;
}

bool execSQL(sqlite3* db, const char* sql, bool printDuplicateColumnErrors = false) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "unknown";
        sqlite3_free(errMsg);

        if (!printDuplicateColumnErrors && err.find("duplicate column name") != std::string::npos) {
            return true;
        }

        std::cerr << "SQLite error: " << err << std::endl;
        return false;
    }

    return true;
}

int toIntParam(const httplib::Request& req, const std::string& name, int defaultValue = 0) {
    if (!req.has_param(name)) {
        return defaultValue;
    }

    try {
        return std::stoi(req.get_param_value(name));
    } catch (...) {
        return defaultValue;
    }
}

double toDoubleParam(const httplib::Request& req, const std::string& name, double defaultValue = 0.0) {
    if (!req.has_param(name)) {
        return defaultValue;
    }

    try {
        return std::stod(req.get_param_value(name));
    } catch (...) {
        return defaultValue;
    }
}

std::string toStringParam(const httplib::Request& req, const std::string& name, const std::string& defaultValue = "") {
    return req.has_param(name) ? req.get_param_value(name) : defaultValue;
}


// Database'i başlatan kısım mainden ayırmak için buraya aldık.
bool initDatabase() {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        std::cerr << "Database could not be opened." << std::endl;
        return false;
    }

    const char* createTrashRecords = R"SQL(
        CREATE TABLE IF NOT EXISTS trash_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            fill_percent INTEGER NOT NULL,
            gas_raw INTEGER NOT NULL,
            gas_voltage REAL,
            gas_do INTEGER NOT NULL,
            distance REAL,
            status TEXT,
            esp_timestamp TEXT,
            emptied INTEGER DEFAULT 0,
            firmware_version TEXT,
            rssi INTEGER DEFAULT 0,
            calibration_empty REAL,
            calibration_full REAL,
            received_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )SQL";

    bool ok = execSQL(db, createTrashRecords);

    // Migration support for old trash_data.db files.
    std::vector<const char*> migrations = {
        "ALTER TABLE trash_records ADD COLUMN firmware_version TEXT;",
        "ALTER TABLE trash_records ADD COLUMN rssi INTEGER DEFAULT 0;",
        "ALTER TABLE trash_records ADD COLUMN calibration_empty REAL;",
        "ALTER TABLE trash_records ADD COLUMN calibration_full REAL;"
    };

    for (const char* sql : migrations) {
        ok = execSQL(db, sql) && ok;
    }

    const char* createNotificationEvents = R"SQL(
        CREATE TABLE IF NOT EXISTS notification_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            level TEXT NOT NULL,
            title TEXT NOT NULL,
            message TEXT NOT NULL,
            channel TEXT NOT NULL,
            delivered INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )SQL";

    ok = execSQL(db, createNotificationEvents) && ok;

    const char* createRecordIndex = R"SQL(
        CREATE INDEX IF NOT EXISTS idx_trash_received_at
        ON trash_records(received_at);
    )SQL";

    const char* createNotificationIndex = R"SQL(
        CREATE INDEX IF NOT EXISTS idx_notification_events_id
        ON notification_events(id);
    )SQL";

    ok = execSQL(db, createRecordIndex) && ok;
    ok = execSQL(db, createNotificationIndex) && ok;

    sqlite3_close(db);

    return ok;
}

// Bu kısım da formatlayıp database e atmak için ama uzun diye buraya aldık
long long insertTrashRecord(
    const std::string& deviceId,
    int fill,
    int gas,
    double gasVoltage,
    int gasDo,
    double distance,
    const std::string& status,
    const std::string& espTimestamp,
    int emptied,
    const std::string& firmwareVersion,
    int rssi,
    double calibrationEmpty,
    double calibrationFull
) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        std::cerr << "Database could not be opened." << std::endl;
        return -1;
    }

    const char* insertSQL = R"SQL(
        INSERT INTO trash_records (
            device_id,
            fill_percent,
            gas_raw,
            gas_voltage,
            gas_do,
            distance,
            status,
            esp_timestamp,
            emptied,
            firmware_version,
            rssi,
            calibration_empty,
            calibration_full
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Prepare error: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, fill);
    sqlite3_bind_int(stmt, 3, gas);
    sqlite3_bind_double(stmt, 4, gasVoltage);
    sqlite3_bind_int(stmt, 5, gasDo);
    sqlite3_bind_double(stmt, 6, distance);
    sqlite3_bind_text(stmt, 7, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, espTimestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, emptied);
    sqlite3_bind_text(stmt, 10, firmwareVersion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, rssi);
    sqlite3_bind_double(stmt, 12, calibrationEmpty);
    sqlite3_bind_double(stmt, 13, calibrationFull);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    long long insertedId = ok ? sqlite3_last_insert_rowid(db) : -1;

    if (!ok) {
        std::cerr << "Insert error: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return insertedId;
}

long long insertNotificationEvent(
    const std::string& level,
    const std::string& title,
    const std::string& message,
    const std::string& channel,
    int delivered
) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return -1;
    }

    const char* sql = R"SQL(
        INSERT INTO notification_events (level, title, message, channel, delivered)
        VALUES (?, ?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, delivered);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    long long id = ok ? sqlite3_last_insert_rowid(db) : -1;

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return id;
}
bool shouldCreateExternalNotification(const std::string& level) {
    if (level != "WARNING" && level != "ALARM") {
        return false;
    }

    static std::map<std::string, std::time_t> lastSentAt;

    std::time_t now = std::time(nullptr);

    auto it = lastSentAt.find(level);
    if (it == lastSentAt.end() || now - it->second >= NOTIFICATION_COOLDOWN_SECONDS) {
        lastSentAt[level] = now;
        return true;
    }

    return false;
}

void handleNotificationIfNeeded(
    const std::string& status,
    const std::string& deviceId,
    int fill,
    int gas,
    int gasDo,
    int emptied
) {
    if (status == "NORMAL" && emptied == 0) {
        return;
    }

    std::string level = status;
    std::string title;
    std::string message;

    if (emptied) {
        level = "INFO";
        title = "Trash Bin Emptied";
        message = "Device " + deviceId + " appears to have been emptied. Current fill level: " + std::to_string(fill) + "%.";
        insertNotificationEvent(level, title, message, "dashboard", 1);
        return;
    }

    if (status == "ALARM") {
        title = "Smart Trash Bin Alarm";
        message = "Device " + deviceId + " reached a critical level. Fill: " +
                  std::to_string(fill) + "%, Gas: " + std::to_string(gas) +
                  ", Digital gas alarm: " + std::to_string(gasDo) + ".";
    } else if (status == "WARNING") {
        title = "Smart Trash Bin Warning";
        message = "Device " + deviceId + " should be monitored. Fill: " +
                  std::to_string(fill) + "%, Gas: " + std::to_string(gas) + ".";
    } else {
        return;
    }

    // Dashboard notification event
    insertNotificationEvent(level, title, message, "dashboard", 1);

    // External notification hook with cooldown
    if (shouldCreateExternalNotification(level)) {
#ifdef _WIN32
    const std::string hook = "notify_hook.bat";
#else
    const std::string hook = "./notify_hook.sh";
#endif
        // hook'un düzgün olduğuna bakıyor
        std::ifstream f(hook);
        if (f.good()) {
            return false;
        }
        
        std::string cmd = hook + " " + shellQuote(level) + " " + shellQuote(title) + " " + shellQuote(message);
        int rc = std::system(cmd.c_str());
        insertNotificationEvent(level, title, message, "external_hook", rc ? 1 : 0);
    }
}

// =========================
// JSON query functions
// =========================
std::string rowToJson(sqlite3_stmt* stmt) {
    std::ostringstream json;

    json << "{";
    json << "\"id\":" << sqlite3_column_int64(stmt, 0) << ",";
    json << "\"device_id\":\"" << jsonEscape(columnText(stmt, 1)) << "\",";
    json << "\"fill\":" << sqlite3_column_int(stmt, 2) << ",";
    json << "\"gas\":" << sqlite3_column_int(stmt, 3) << ",";
    json << "\"gas_voltage\":" << sqlite3_column_double(stmt, 4) << ",";
    json << "\"gas_do\":" << (sqlite3_column_int(stmt, 5) ? "true" : "false") << ",";
    json << "\"distance\":" << sqlite3_column_double(stmt, 6) << ",";
    json << "\"status\":\"" << jsonEscape(columnText(stmt, 7)) << "\",";
    json << "\"esp_timestamp\":\"" << jsonEscape(columnText(stmt, 8)) << "\",";
    json << "\"emptied\":" << (sqlite3_column_int(stmt, 9) ? "true" : "false") << ",";
    json << "\"firmware\":\"" << jsonEscape(columnText(stmt, 10)) << "\",";
    json << "\"rssi\":" << sqlite3_column_int(stmt, 11) << ",";
    json << "\"calibration_empty\":" << sqlite3_column_double(stmt, 12) << ",";
    json << "\"calibration_full\":" << sqlite3_column_double(stmt, 13) << ",";
    json << "\"received_at\":\"" << jsonEscape(columnText(stmt, 14)) << "\"";
    json << "}";

    return json.str();
}
std::string get_local_ip(){
	struct ifaddrs *myaddrs, *ifa;
	void *in_addr;
	char buf[64];
	std::string ip;
	if(getifaddrs(&myaddrs) != 0){
		perror("getifaddrs");
		exit(1);
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next){
		if (ifa->ifa_addr == NULL)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;

		switch (ifa->ifa_addr->sa_family){
			case AF_INET:{
				struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
				in_addr = &s4->sin_addr;
				break;
			}

			case AF_INET6:{
				struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
				in_addr = &s6->sin6_addr;
				break;
			}

			default:
				continue;
		}

		if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))){
			printf("%s: inet_ntop failed!\n", ifa->ifa_name);
		}
		else{
			if(((std::string)ifa->ifa_name) == "wlan0"){
				printf("%s: %s\n", ifa->ifa_name, buf);
				return buf;
			}
		}
	}

	freeifaddrs(myaddrs);
	return ip;
}
std::string getLiveJson() {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return "{\"error\":\"database_open_failed\"}";
    }

    const char* sql = R"SQL(
        SELECT
            id,
            device_id,
            fill_percent,
            gas_raw,
            gas_voltage,
            gas_do,
            distance,
            status,
            esp_timestamp,
            emptied,
            firmware_version,
            rssi,
            calibration_empty,
            calibration_full,
            received_at
        FROM trash_records
        ORDER BY id DESC
        LIMIT 1;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"error\":\"prepare_failed\"}";
    }

    std::string result = "{}";

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToJson(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return result;
}

std::string getHistoryJson(int limit) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return "{\"error\":\"database_open_failed\"}";
    }

    const char* sql = R"SQL(
        SELECT
            id,
            device_id,
            fill_percent,
            gas_raw,
            gas_voltage,
            gas_do,
            distance,
            status,
            esp_timestamp,
            emptied,
            firmware_version,
            rssi,
            calibration_empty,
            calibration_full,
            received_at
        FROM trash_records
        ORDER BY id DESC
        LIMIT ?;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"error\":\"prepare_failed\"}";
    }

    sqlite3_bind_int(stmt, 1, limit);

    std::ostringstream json;
    json << "[";

    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) {
            json << ",";
        }

        first = false;
        json << rowToJson(stmt);
    }

    json << "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return json.str();
}

std::string getStatsJson() {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return "{\"error\":\"database_open_failed\"}";
    }

    const char* sql = R"SQL(
        SELECT
            COUNT(*),
            COALESCE(MAX(fill_percent), 0),
            COALESCE(MIN(fill_percent), 0),
            COALESCE(ROUND(AVG(fill_percent)), 0),
            COALESCE(MAX(gas_raw), 0),
            COALESCE(SUM(emptied), 0),
            COALESCE(SUM(CASE WHEN status = 'WARNING' THEN 1 ELSE 0 END), 0),
            COALESCE(SUM(CASE WHEN status = 'ALARM' THEN 1 ELSE 0 END), 0)
        FROM trash_records
        WHERE DATE(received_at) = DATE('now', 'localtime');
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"error\":\"prepare_failed\"}";
    }

    std::ostringstream json;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json << "{";
        json << "\"count\":" << sqlite3_column_int(stmt, 0) << ",";
        json << "\"max_fill\":" << sqlite3_column_int(stmt, 1) << ",";
        json << "\"min_fill\":" << sqlite3_column_int(stmt, 2) << ",";
        json << "\"avg_fill\":" << sqlite3_column_int(stmt, 3) << ",";
        json << "\"max_gas\":" << sqlite3_column_int(stmt, 4) << ",";
        json << "\"empties\":" << sqlite3_column_int(stmt, 5) << ",";
        json << "\"warnings\":" << sqlite3_column_int(stmt, 6) << ",";
        json << "\"alarms\":" << sqlite3_column_int(stmt, 7);
        json << "}";
    } else {
        json << "{}";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return json.str();
}

std::string getNotificationsJson(long long afterId) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return "{\"error\":\"database_open_failed\"}";
    }

    const char* sql = R"SQL(
        SELECT id, level, title, message, channel, delivered, created_at
        FROM notification_events
        WHERE id > ?
        ORDER BY id ASC
        LIMIT 50;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"error\":\"prepare_failed\"}";
    }

    sqlite3_bind_int64(stmt, 1, afterId);

    std::ostringstream json;
    json << "[";

    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) {
            json << ",";
        }

        first = false;

        json << "{";
        json << "\"id\":" << sqlite3_column_int64(stmt, 0) << ",";
        json << "\"level\":\"" << jsonEscape(columnText(stmt, 1)) << "\",";
        json << "\"title\":\"" << jsonEscape(columnText(stmt, 2)) << "\",";
        json << "\"message\":\"" << jsonEscape(columnText(stmt, 3)) << "\",";
        json << "\"channel\":\"" << jsonEscape(columnText(stmt, 4)) << "\",";
        json << "\"delivered\":" << (sqlite3_column_int(stmt, 5) ? "true" : "false") << ",";
        json << "\"created_at\":\"" << jsonEscape(columnText(stmt, 6)) << "\"";
        json << "}";
    }

    json << "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return json.str();
}

std::string getCsvExport() {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* db = nullptr;

    if (sqlite3_open(DB_NAME, &db) != SQLITE_OK) {
        return "error,database_open_failed\n";
    }

    const char* sql = R"SQL(
        SELECT
            id,
            device_id,
            fill_percent,
            gas_raw,
            gas_voltage,
            gas_do,
            distance,
            status,
            emptied,
            firmware_version,
            rssi,
            calibration_empty,
            calibration_full,
            received_at
        FROM trash_records
        ORDER BY id ASC;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "error,prepare_failed\n";
    }

    std::ostringstream csv;
    csv << "id,device_id,fill_percent,gas_raw,gas_voltage,gas_do,distance,status,emptied,firmware,rssi,calibration_empty,calibration_full,received_at\n";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        csv << sqlite3_column_int64(stmt, 0) << ",";
        csv << "\"" << jsonEscape(columnText(stmt, 1)) << "\",";
        csv << sqlite3_column_int(stmt, 2) << ",";
        csv << sqlite3_column_int(stmt, 3) << ",";
        csv << sqlite3_column_double(stmt, 4) << ",";
        csv << sqlite3_column_int(stmt, 5) << ",";
        csv << sqlite3_column_double(stmt, 6) << ",";
        csv << "\"" << jsonEscape(columnText(stmt, 7)) << "\",";
        csv << sqlite3_column_int(stmt, 8) << ",";
        csv << "\"" << jsonEscape(columnText(stmt, 9)) << "\",";
        csv << sqlite3_column_int(stmt, 10) << ",";
        csv << sqlite3_column_double(stmt, 11) << ",";
        csv << sqlite3_column_double(stmt, 12) << ",";
        csv << "\"" << jsonEscape(columnText(stmt, 13)) << "\"\n";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return csv.str();
}

// =========================
// Main server
// =========================
int main() {
    if (!initDatabase()) {
        std::cerr << "Database could not be initialized." << std::endl;
        return 1;
    }

    httplib::Server server;

    server.set_mount_point("/static", "./public");

    auto setCors = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    };

    server.Options(R"(.*)", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);
    });

    server.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("./public/index.html", std::ios::binary);
        if (!file) {
            // TODO handle error
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string html = ss.str();

        if (html.empty()) {
            res.status = 404;
            res.set_content("public/index.html not found.", "text/plain; charset=utf-8");
            return;
        }

        res.set_content(html, "text/html; charset=utf-8");
    });
    server.Get("/style.css", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("./public/style.css", std::ios::binary);
        if (!file) {
            // TODO handle error
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string html = ss.str();

        if (html.empty()) {
            res.status = 404;
            res.set_content("public/style.css not found.", "text/plain; charset=utf-8");
            return;
        }

        res.set_content(html, "text/css; charset=utf-8");
    });
    server.Get("/app.js", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("./public/app.js", std::ios::binary);
        if (!file) {
            // TODO handle error
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string html = ss.str();

        if (html.empty()) {
            res.status = 404;
            res.set_content("public/app.js not found.", "text/plain; charset=utf-8");
            return;
        }

        res.set_content(html, "application/javascript; charset=utf-8");
    });

    server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    server.Post("/api/trash", [&](const httplib::Request& req, httplib::Response& res) {
        setCors(res);

        try {
            if (!req.has_param("fill") ||
                !req.has_param("gas") ||
                !req.has_param("gas_voltage") ||
                !req.has_param("gas_do") ||
                !req.has_param("distance")) {

                res.status = 400;
                res.set_content("{\"error\":\"missing_required_fields\"}", "application/json");
                return;
            }

            std::string deviceId = toStringParam(req, "device_id", "trash_bin_01");
            int fill = toIntParam(req, "fill", 0);
            int gas = toIntParam(req, "gas", 0);
            double gasVoltage = toDoubleParam(req, "gas_voltage", 0.0);
            int gasDo = toIntParam(req, "gas_do", 0);
            double distance = toDoubleParam(req, "distance", 0.0);
            int emptied = toIntParam(req, "emptied", 0);
            std::string timestamp = toStringParam(req, "timestamp", "");
            std::string firmware = toStringParam(req, "firmware", "");
            int rssi = toIntParam(req, "rssi", 0);
            double calibrationEmpty = toDoubleParam(req, "cal_empty", 0.0);
            double calibrationFull = toDoubleParam(req, "cal_full", 0.0);

            std::string status = toStringParam(req, "status", "");
            if (status.empty()) {
                status = "NORMAL";
                if (fill >= FILL_WARNING || gas >= GAS_WARNING) {
                    status = "WARNING";
                }
                if (fill >= FILL_ALARM || gas >= GAS_ALARM || gasDo != 0) {
                    status = "ALARM";
                }
            }

            long long id = insertTrashRecord(
                deviceId,
                fill,
                gas,
                gasVoltage,
                gasDo,
                distance,
                status,
                timestamp,
                emptied,
                firmware,
                rssi,
                calibrationEmpty,
                calibrationFull
            );

            if (id < 0) {
                res.status = 500;
                res.set_content("{\"error\":\"database_insert_failed\"}", "application/json");
                return;
            }

            handleNotificationIfNeeded(status, deviceId, fill, gas, gasDo, emptied);

            std::cout
                << "Record saved | id=" << id
                << " device=" << deviceId
                << " fill=" << fill
                << " gas=" << gas
                << " status=" << status
                << " emptied=" << emptied
                << std::endl;

            std::ostringstream json;
            json << "{\"message\":\"saved\",\"id\":" << id << ",\"status\":\"" << jsonEscape(status) << "\"}";
            res.set_content(json.str(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;

            std::string err = std::string("{\"error\":\"bad_request\",\"detail\":\"") +
                              jsonEscape(e.what()) +
                              "\"}";

            res.set_content(err, "application/json");
        }
    });

    server.Get("/api/live", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.set_content(getLiveJson(), "application/json");
    });

    server.Get("/api/history", [&](const httplib::Request& req, httplib::Response& res) {
        setCors(res);

        int limit = 100;

        if (req.has_param("limit")) {
            limit = toIntParam(req, "limit", 100);
        }

        limit = std::max(1, std::min(1000, limit));

        res.set_content(getHistoryJson(limit), "application/json");
    });

    server.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.set_content(getStatsJson(), "application/json");
    });

    server.Get("/api/notifications", [&](const httplib::Request& req, httplib::Response& res) {
        setCors(res);

        long long afterId = 0;
        if (req.has_param("after_id")) {
            try {
                afterId = std::stoll(req.get_param_value("after_id"));
            } catch (...) {
                afterId = 0;
            }
        }

        res.set_content(getNotificationsJson(afterId), "application/json");
    });

    server.Post("/api/test-notification", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);

        long long id = insertNotificationEvent(
            "INFO",
            "Test Notification",
            "Dashboard notification pipeline is working.",
            "dashboard",
            1
        );

        res.set_content("{\"message\":\"created\",\"id\":" + std::to_string(id) + "}", "application/json");
    });
    
    server.Get("/api/export.csv", [&](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.set_header("Content-Disposition", "attachment; filename=\"trash_data_export.csv\"");
        res.set_content(getCsvExport(), "text/csv; charset=utf-8");
    });
    
    std::cout << "Smart Trash Bin server is running." << std::endl;
    std::cout << "Dashboard : http://localhost:" << SERVER_PORT << "/" << std::endl;
    std::cout << "Live API  : http://localhost:" << SERVER_PORT << "/api/live" << std::endl;
    std::cout << "History   : http://localhost:" << SERVER_PORT << "/api/history?limit=100" << std::endl;
    std::cout << "Stats     : http://localhost:" << SERVER_PORT << "/api/stats" << std::endl;
    std::cout << "Export    : http://localhost:" << SERVER_PORT << "/api/export.csv" << std::endl;
    std::string ip = get_local_ip();
    std::cout << ip <<"\n";
    server.listen(ip, SERVER_PORT);
    
    return 0;
}
