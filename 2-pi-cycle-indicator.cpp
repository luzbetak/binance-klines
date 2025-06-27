#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>

// For JSON parsing (nlohmann/json)
#include "json.hpp"
using json = nlohmann::json;

// For HTTP requests (libcurl)
#include <curl/curl.h>

// For SQLite
#include <sqlite3.h>

// --- Global Constants (for simplicity, should be in config or env vars) ---
const std::string DB_PATH = "binance.db";
const std::string GEMINI_API_URL = "https://api.gemini.com/v1/pubticker/btcusd";

// --- Global variables to mimic Python's global state ---
double first_row_yearly_value = 0.00;
double first_row_baseline = 0.00;
double first_row_avg_price = 0.00;
double first_row_step = 0.00;

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

// --- Kline Data Structure (from SQLite) ---
struct Kline {
    std::string date; // YYYY-MM-DD
    double price;
};

// --- Pi Cycle Indicator Data Structure (mimics DataFrame rows) ---
struct PiCycleData {
    std::string date;
    double price;
    double ma_365 = 0.0;      // 365-day Moving Average
    double std_365 = 0.0;     // 365-day Standard Deviation
    double ceiling = 0.0;     // MA_365 + (2 * STD_365)
    double floor = 0.0;       // MA_365
    double median = 0.0;      // (CEILING + FLOOR) / 2
    double dynamic_step = 0.0; // Dynamic step based on 364-day Price analysis
    double step = 0.0;        // Same as dynamic_step
    double change = 0.0;      // Daily price change
    double move = 0.0;        // Daily price percentage change
    double offset = 0.0;      // Percentage distance from MEDIAN
    double weeks_52 = 0.0;    // 52-week price percentage change
};

// --- SQLite Functions ---
std::vector<Kline> fetch_data() {
    std::vector<Kline> klines_data;
    sqlite3* db;
    int rc = sqlite3_open(DB_PATH.c_str(), &db);

    if (rc) {
        std::cerr << "Error: Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return klines_data;
    } else {
        std::cout << "Debug: Database opened successfully." << std::endl;
    }

    std::string query = "SELECT dt1 AS Date, price AS Price FROM klines ORDER BY Date ASC;";
    sqlite3_stmt* stmt;

    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Debug: Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return klines_data;
    } else {
        std::cout << "Debug: SQL statement prepared successfully." << std::endl;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Kline kline;
        kline.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        kline.price = sqlite3_column_double(stmt, 1);
        klines_data.push_back(kline);
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "Execution failed: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::cout << "Debug: Fetched " << klines_data.size() << " klines from database." << std::endl;
    return klines_data;
}

// --- SQLite Functions (continued) ---
double calculate_average_daily_increase(int days) {
    sqlite3* db;
    int rc = sqlite3_open(DB_PATH.c_str(), &db);

    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 0.0;
    }

    std::string query = "";
    // SQLite doesn't have INTERVAL, so we need to use date function
    query = "SELECT ROUND(AVG(daily_increase), 4) AS avg_daily_increase FROM ( SELECT dt1, (price - LAG(price) OVER (ORDER BY dt1)) AS daily_increase FROM klines WHERE dt1 >= date('now', '-' || ? || ' days') ) AS price_changes WHERE daily_increase IS NOT NULL;";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 0.0;
    }

    sqlite3_bind_int(stmt, 1, days);

    double avg_daily_increase = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        avg_daily_increase = sqlite3_column_double(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return avg_daily_increase;
}

// --- Gemini API Function ---
struct GeminiTicker {
    double bid;
    double ask;
    double last;
};

GeminiTicker gemini_get_bid_ask_last() {
    GeminiTicker ticker = {0.0, 0.0, 0.0};
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, GEMINI_API_URL.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // WARNING: For testing, disable in production
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // WARNING: For testing, disable in production

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            try {
                json data = json::parse(readBuffer);
                ticker.bid = std::stod(data["bid"].get<std::string>());
                ticker.ask = std::stod(data["ask"].get<std::string>());
                ticker.last = std::stod(data["last"].get<std::string>());
            } catch (const json::parse_error& e) {
                std::cerr << "JSON parse error: " << e.what() << std::endl;
                std::cerr << "Received data: " << readBuffer.substr(0, std::min((int)readBuffer.length(), 500)) << "..." << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error processing Gemini ticker data: " << e.what() << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return ticker;
}

// --- Price Projection Function ---
std::vector<PiCycleData> price_projection(const std::vector<Kline>& klines, double yearly_multiplier) {
    std::vector<PiCycleData> pi_data(klines.size());

    for (size_t i = 0; i < klines.size(); ++i) {
        pi_data[i].date = klines[i].date;
        pi_data[i].price = klines[i].price;

        if (i >= 364) { // Need 365 data points for 365-day MA and STD
            double sum_price = 0.0;
            for (size_t j = i - 364; j <= i; ++j) {
                sum_price += klines[j].price;
            }
            pi_data[i].ma_365 = sum_price / 365.0;

            double sum_sq_diff = 0.0;
            for (size_t j = i - 364; j <= i; ++j) {
                sum_sq_diff += std::pow(klines[j].price - pi_data[i].ma_365, 2);
            }
            pi_data[i].std_365 = std::sqrt(sum_sq_diff / 365.0);

            pi_data[i].ceiling = pi_data[i].ma_365 + (2 * pi_data[i].std_365);
            pi_data[i].floor = pi_data[i].ma_365;
            pi_data[i].median = (pi_data[i].ceiling + pi_data[i].floor) / 2.0;
        }
    }
    return pi_data;
}

// --- Add Calculated Fields Function ---
std::vector<PiCycleData> add_calculated_fields(std::vector<PiCycleData> pi_data, int num_display_days) {
    // Calculate daily price changes
    for (size_t i = 1; i < pi_data.size(); ++i) {
        pi_data[i].change = pi_data[i].price - pi_data[i-1].price;
        if (pi_data[i-1].price != 0) {
            pi_data[i].move = (pi_data[i].price - pi_data[i-1].price) / pi_data[i-1].price * 100.0;
        }
    }

    // Calculate dynamic step (simplified: rolling average of daily changes)
    const int lookback_days_for_step = 364; // Corresponds to 364-day lookback in Python
    for (size_t i = 0; i < pi_data.size(); ++i) {
        if (i >= lookback_days_for_step) {
            double sum_daily_diff = 0.0;
            for (int j = 0; j < lookback_days_for_step; ++j) {
                sum_daily_diff += pi_data[i - j].change; // Summing up daily changes
            }
            pi_data[i].dynamic_step = sum_daily_diff / lookback_days_for_step;
        }
        pi_data[i].step = pi_data[i].dynamic_step;
    }

    // Calculate Offset - distance from MEDIAN as percentage
    for (size_t i = 0; i < pi_data.size(); ++i) {
        if (pi_data[i].median != 0) {
            pi_data[i].offset = ((pi_data[i].price - pi_data[i].median) / pi_data[i].median) * 100.0;
        }
    }

    // Calculate actual 364-day price percentage change (current price vs price 364 days ago)
    for (size_t i = 0; i < pi_data.size(); ++i) {
        if (i >= 364) {
            if (pi_data[i - 364].price != 0) {
                pi_data[i].weeks_52 = ((pi_data[i].price - pi_data[i - 364].price) / pi_data[i - 364].price) * 100.0;
            }
        }
    }

    return pi_data;
}

// Helper to format numeric values with commas
std::string format_numeric(double value, const std::string& format_spec) {
    if (std::isnan(value)) return "";
    std::stringstream ss;
    if (format_spec.find("f") != std::string::npos) { // Floating point
        int precision = std::stoi(format_spec.substr(format_spec.find(".") + 1, format_spec.find("f") - format_spec.find(".") - 1));
        ss << std::fixed << std::setprecision(precision);
    } else { // Integer or general
        ss << std::fixed << std::setprecision(0);
    }
    ss << value;
    std::string s = ss.str();
    // Add commas for thousands (simple implementation for integer part)
    size_t decimal_point_pos = s.find('.');
    std::string integer_part = (decimal_point_pos == std::string::npos) ? s : s.substr(0, decimal_point_pos);
    std::string fractional_part = (decimal_point_pos == std::string::npos) ? "" : s.substr(decimal_point_pos);

    int insertPosition = integer_part.length() - 3;
    while (insertPosition > 0 && integer_part[insertPosition-1] != '-') {
        integer_part.insert(insertPosition, ",");
        insertPosition -= 3;
    }
    return integer_part + fractional_part;
};

// --- Display Functions ---
void display_public(const std::vector<PiCycleData>& pi_data_reversed) {
    // Constants for color codes
    const std::string COLOR_BRIGHT_GREEN = "\033[92m";
    const std::string COLOR_GREEN = "\033[32m";
    const std::string COLOR_DARK_GREEN = "\033[38;5;22m";
    const std::string COLOR_YELLOW_GREEN = "\033[38;5;142m";
    const std::string COLOR_YELLOW = "\033[93m";
    const std::string COLOR_YELLOW_RED = "\033[38;5;208m";
    const std::string COLOR_DARK_RED = "\033[38;5;52m";
    const std::string COLOR_RED = "\033[91m";
    const std::string COLOR_BRIGHT_RED = "\033[38;5;196m";
    const std::string COLOR_RESET = "\033[0m";

    // Column definitions with their formatting specifications
    static const std::map<std::string, std::map<std::string, std::string>> COLUMN_FORMATS = {
        {"Date",      {{"width", "10"}, {"align", "<"}, {"prefix", " "}}},
        {"Price",     {{"width", "10"},  {"align", ">"}, {"prefix", ""}}},
        {"Move",      {{"width", "7"},  {"align", ">"}, {"prefix", " "}}},
        {"Offset",    {{"width", "7"},  {"align", ">"}, {"prefix", ""}}},
        {"CEILING",   {{"width", "10"},  {"align", ">"}, {"prefix", ""}}},
        {" MEDIAN",   {{"width", "10"},  {"align", ">"}, {"prefix", ""}}},
        {" FLOOR ",   {{"width", "10"},  {"align", ">"}, {"prefix", ""}}},
        {"Step",      {{"width", "5"},  {"align", ">"}, {"prefix", ""}}},
        {"Change",    {{"width", "7"},  {"align", ">"}, {"prefix", ""}}},
        {"52-weeks",  {{"width", "8"},  {"align", "^"}, {"prefix", " "}}}
    };

    // Print header
    std::cout << "+------------+-----------+--------+---------+----------+----------+----------+------+--------+----------+" << std::endl;
    std::cout << "|    Date    |   Price   |  Move  | Offset  | CEILING  |  MEDIAN  |  FLOOR   | Step | Change | 52-weeks |" << std::endl;
    std::cout << "+------------+-----------+--------+---------+----------+----------+----------+------+--------+----------+" << std::endl;

    // Extract first row values for global variables
    if (!pi_data_reversed.empty()) {
        const auto& first_row = pi_data_reversed[0];
        first_row_yearly_value = first_row.weeks_52;
        first_row_baseline = first_row.median;
        first_row_step = first_row.step;
        first_row_avg_price = first_row.price;
    }

    for (const auto& row : pi_data_reversed) {
        std::string row_color = COLOR_RESET; // Default to reset

        // Determine color based on price position
        double median_threshold = row.median * 0.02;
        if (std::abs(row.price - row.median) <= median_threshold) {
            if (row.price > row.median) row_color = COLOR_YELLOW_GREEN;
            else if (row.price < row.median) row_color = COLOR_YELLOW_RED;
            else row_color = COLOR_YELLOW;
        } else if (row.price >= row.median) {
            double range_above = row.ceiling - row.median;
            double percentage_above = (range_above > 0) ? (row.price - row.median) / range_above : 0.0;
            if (percentage_above >= 0.575) row_color = COLOR_BRIGHT_GREEN;
            else if (percentage_above >= 0.29) row_color = COLOR_GREEN;
            else row_color = COLOR_DARK_GREEN;
        } else {
            double range_below = row.median - row.floor;
            double percentage_below = (range_below > 0) ? (row.median - row.price) / range_below : 0.0;
            if (percentage_below >= 0.575) row_color = COLOR_BRIGHT_RED;
            else if (percentage_below >= 0.29) row_color = COLOR_RED;
            else row_color = COLOR_DARK_RED;
        }

        std::stringstream ss_row;
        ss_row << "|";

        // Date
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Date").at("width"))) << std::left << (COLUMN_FORMATS.at("Date").at("prefix") + row.date) << " |";

        // Price
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Price").at("width"))) << std::right << format_numeric(row.price, "0f") << " |";

        // Move
        std::stringstream ss_move;
        ss_move << std::fixed << std::setprecision(2) << row.move << "%";
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Move").at("width"))) << std::right << (COLUMN_FORMATS.at("Move").at("prefix") + ss_move.str()) << " |";

        // Offset
        std::stringstream ss_offset;
        ss_offset << std::fixed << std::setprecision(1) << row.offset << "%";
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Offset").at("width"))) << std::right << (COLUMN_FORMATS.at("Offset").at("prefix") + ss_offset.str()) << " |";

        // CEILING
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("CEILING").at("width"))) << std::right << format_numeric(row.ceiling, "0f") << " |";

        // MEDIAN
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at(" MEDIAN").at("width"))) << std::right << format_numeric(row.median, "0f") << " |";

        // FLOOR
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at(" FLOOR ").at("width"))) << std::right << format_numeric(row.floor, "0f") << " |";

        // Step
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Step").at("width"))) << std::right << format_numeric(row.step, "0f") << " |";

        // Change
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("Change").at("width"))) << std::right << format_numeric(row.change, "0f") << " |";

        // 52-weeks
        std::stringstream ss_52weeks;
        ss_52weeks << std::fixed << std::setprecision(2) << row.weeks_52 << "%";
        ss_row << std::setw(std::stoi(COLUMN_FORMATS.at("52-weeks").at("width"))) << std::right << (COLUMN_FORMATS.at("52-weeks").at("prefix") + ss_52weeks.str()) << " |";

        std::cout << row_color << ss_row.str() << COLOR_RESET << std::endl;
    }
    std::cout << "+------------+-----------+--------+---------+----------+----------+----------+------+--------+----------+" << std::endl;
}

void prediction_target_step(const std::vector<PiCycleData>& pi_data_reversed) {
    const int RANGE = 30;
    double sum_top_steps = 0.0;
    int count_top_steps = 0;

    // Get the last RANGE step values
    for (int i = 0; i < RANGE && i < pi_data_reversed.size(); ++i) {
        sum_top_steps += pi_data_reversed[i].step;
        count_top_steps++;
    }

    double average_top_steps = (count_top_steps > 0) ? sum_top_steps / count_top_steps : 0.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "| " << RANGE << "-day Avg Step: " << average_top_steps << " (Dynamic 364-day Price-based)" << std::endl;

    // Get current date
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* ptm_now = std::localtime(&now_c);

    // Calculate days until end of 2025
    std::tm end_2025_tm = *ptm_now; // Copy current time info
    end_2025_tm.tm_year = 2025 - 1900; // Year is 1900-based
    end_2025_tm.tm_mon = 11; // Month is 0-based (December)
    end_2025_tm.tm_mday = 31; // Day of month
    std::time_t end_2025_t = std::mktime(&end_2025_tm);
    
    long long days_until_2025 = (end_2025_t - now_c) / (60 * 60 * 24);

    // Predictions
    double predicted_price_2025 = first_row_baseline + (average_top_steps * days_until_2025);
    double predicted_price_4w = first_row_baseline + (average_top_steps * RANGE);

    // Calculate dates for predictions
    auto date_4w = now + std::chrono::hours(24 * RANGE);
    std::time_t date_4w_c = std::chrono::system_clock::to_time_t(date_4w);
    std::tm* ptm_4w = std::localtime(&date_4w_c);

    std::cout << "+----------+----------+-------------------+" << std::endl;
    std::cout << "|    2025  | " << std::setw(8) << std::right << "$" << std::fixed << std::setprecision(0) << predicted_price_2025 << " | "
              << std::put_time(&end_2025_tm, "%B %d, %Y") << " |" << std::endl;
    std::cout << "|    +4w   | " << std::setw(8) << std::right << "$" << std::fixed << std::setprecision(0) << predicted_price_4w << " | "
              << std::put_time(ptm_4w, "%B %d, %Y") << " |" << std::endl;
    std::cout << "+----------+----------+-------------------+" << std::endl;
}

// --- Main Function ---
int main(int argc, char* argv[]) {
    // Clear screen (platform-dependent)
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif

    int num_display_days = 33;
    if (argc > 1) {
        try {
            num_display_days = std::stoi(argv[1]);
            if (num_display_days < 33) num_display_days = 33;
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid argument for num_display_days: " << argv[1] << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "num_display_days out of range: " << argv[1] << std::endl;
        }
    }

    double avg_daily_increase = calculate_average_daily_increase(365 * 4 + 1);
    double compound_4_year = (std::pow(1 + avg_daily_increase / 100.0, 4) - 1) * 100.0;

    // ANSI escape codes for colors
    const std::string COLOR_YELLOW = "\033[93m";
    const std::string COLOR_RESET = "\033[0m";

    std::cout << COLOR_YELLOW << std::fixed << std::setprecision(2)
              << "                               4-year Avg: " << avg_daily_increase << "%/year -> "
              << compound_4_year << "% compound                  " << COLOR_RESET << std::endl;

    std::cout << "Fetching data from SQLite..." << std::endl;
    std::vector<Kline> klines = fetch_data();

    if (klines.empty()) {
        std::cerr << "No klines data fetched. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Debug: Fetched " << klines.size() << " klines." << std::endl;

    std::vector<PiCycleData> pi_data = price_projection(klines, (avg_daily_increase / 100.0) + 1);
    pi_data = add_calculated_fields(pi_data, num_display_days);

    // Filter for last num_display_days and reverse for display
    std::vector<PiCycleData> pi_data_reversed;
    if (pi_data.size() > num_display_days) {
        pi_data_reversed.assign(pi_data.end() - num_display_days, pi_data.end());
    } else {
        pi_data_reversed = pi_data;
    }
    std::reverse(pi_data_reversed.begin(), pi_data_reversed.end());

    display_public(pi_data_reversed);

    GeminiTicker ticker = gemini_get_bid_ask_last();
    // std::cout << "Gemini Ticker: Bid=" << ticker.bid << ", Ask=" << ticker.ask << ", Last=" << ticker.last << std::endl; // Remove debug print

    prediction_target_step(pi_data_reversed); // Pass pi_data_reversed for step calculation

    return 0;
}