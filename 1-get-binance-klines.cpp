#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdlib> // For getenv

// For JSON parsing (nlohmann/json)
#include "json.hpp"
using json = nlohmann::json;

// For HTTP requests (libcurl)
#include <curl/curl.h>

// For SQLite
#include <sqlite3.h>

// --- Global Constants ---
const std::string BASE_URL = "https://api.binance.us"; // Or https://api.binance.com for global
const std::string DB_PATH = "binance.db";

// --- Helper function for cURL write callback ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch (std::bad_alloc &e) {
        std::cerr << "Memory allocation error in cURL callback: " << e.what() << std::endl;
        return 0;
    }
    return newLength;
}

// --- Kline Data Structure ---
struct Kline {
    std::string dt1; // Date string YYYY-MM-DD
    double price;
    double open;
    double high;
    double low;
    double close;
    double volume;
    int num_trades;
};

// --- SQLite Functions ---
void create_klines_table(sqlite3* db) {
    char* err_msg = 0;
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS klines (
            dt1 DATE,
            price REAL,
            open REAL,
            high REAL,
            low REAL,
            close REAL,
            volume REAL,
            num_trades INTEGER,
            UNIQUE (dt1)
        );
    )";
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error (create_klines_table): " << err_msg << std::endl;
        sqlite3_free(err_msg);
    } else {
        std::cout << "Table 'klines' checked/created successfully." << std::endl;
    }
}

void insert_klines_data(sqlite3* db, const std::vector<Kline>& klines) {
    std::string sql = R"(
        INSERT INTO klines (dt1, price, open, high, low, close, volume, num_trades)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(dt1) DO UPDATE SET
            price      = excluded.price,
            open       = excluded.open,
            high       = excluded.high,
            low        = excluded.low,
            close      = excluded.close,
            volume     = excluded.volume,
            num_trades = excluded.num_trades;
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    for (const auto& kline : klines) {
        sqlite3_bind_text(stmt, 1, kline.dt1.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 2, kline.price);
        sqlite3_bind_double(stmt, 3, kline.open);
        sqlite3_bind_double(stmt, 4, kline.high);
        sqlite3_bind_double(stmt, 5, kline.low);
        sqlite3_bind_double(stmt, 6, kline.close);
        sqlite3_bind_double(stmt, 7, kline.volume);
        sqlite3_bind_int(stmt, 8, kline.num_trades);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Execution failed: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "END TRANSACTION;", NULL, NULL, NULL);
    std::cout << "Klines data inserted/updated successfully." << std::endl;
}

void update_current_date_price_with_close(sqlite3* db) {
    char* err_msg = 0;
    std::string sql = R"(
        UPDATE klines
        SET price = close
        WHERE dt1 = (SELECT MAX(dt1) FROM klines);
    )";
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error (update_current_date_price_with_close): " << err_msg << std::endl;
        sqlite3_free(err_msg);
    } else {
        // Get the date that was updated for logging
        sqlite3_stmt* stmt;
        std::string select_max_date_sql = "SELECT MAX(dt1) FROM klines;";
        rc = sqlite3_prepare_v2(db, select_max_date_sql.c_str(), -1, &stmt, 0);
        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* latest_date = sqlite3_column_text(stmt, 0);
                if (latest_date) {
                    std::cout << "Updated price with close price for latest date: " << latest_date << std::endl;
                } else {
                    std::cout << "No records found to update in the klines table." << std::endl;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
}

// --- Binance API Function ---
std::vector<Kline> get_klines_from_binance() {
    std::vector<Kline> klines_data;
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        std::string url = BASE_URL + "/api/v3/klines?symbol=BTCUSDT&interval=1d&limit=500";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // WARNING: For testing, disable in production
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // WARNING: For testing, disable in production

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            try {
                json klines_json = json::parse(readBuffer);
                if (klines_json.is_array()) {
                    for (const auto& kline_array : klines_json) {
                        Kline kline;
                        // Convert timestamp (ms) to date string (YYYY-MM-DD)
                        long long open_time_ms = kline_array[0].get<long long>();
                        std::chrono::system_clock::time_point open_time_tp = std::chrono::system_clock::from_time_t(open_time_ms / 1000);
                        std::time_t open_time_t = std::chrono::system_clock::to_time_t(open_time_tp);
                        std::tm* ptm = std::gmtime(&open_time_t); // Use gmtime for UTC
                        std::stringstream ss;
                        ss << std::put_time(ptm, "%Y-%m-%d");
                        kline.dt1 = ss.str();

                        kline.open = std::stod(kline_array[1].get<std::string>());
                        kline.high = std::stod(kline_array[2].get<std::string>());
                        kline.low = std::stod(kline_array[3].get<std::string>());
                        kline.close = std::stod(kline_array[4].get<std::string>());
                        kline.volume = std::stod(kline_array[5].get<std::string>());
                        kline.num_trades = kline_array[8].get<int>();

                        // Calculate price as (high + low) / 2, rounded to 2 decimals
                        kline.price = std::round(((kline.high + kline.low) / 2.0) * 100.0) / 100.0;

                        klines_data.push_back(kline);
                    }
                    std::cout << "Fetched " << klines_data.size() << " klines from Binance." << std::endl;
                } else if (klines_json.is_object() && klines_json.contains("msg")) {
                    std::cerr << "Binance API Error: " << klines_json["msg"].get<std::string>() << std::endl;
                } else {
                    std::cerr << "Unexpected JSON response from Binance API." << std::endl;
                }
            } catch (const json::parse_error& e) {
                std::cerr << "JSON parse error: " << e.what() << std::endl;
                std::cerr << "Received data: " << readBuffer.substr(0, std::min((int)readBuffer.length(), 500)) << "..." << std::endl; // Print first 500 chars
            } catch (const std::exception& e) {
                std::cerr << "Error processing klines data: " << e.what() << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return klines_data;
}

int main() {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(DB_PATH.c_str(), &db);

    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    } else {
        std::cout << "Opened database successfully." << std::endl;
    }

    create_klines_table(db);

    std::vector<Kline> klines = get_klines_from_binance();
    if (!klines.empty()) {
        insert_klines_data(db, klines);
        update_current_date_price_with_close(db);
    } else {
        std::cerr << "No klines data fetched. Skipping database operations." << std::endl;
    }

    sqlite3_close(db);
    return 0;
}
