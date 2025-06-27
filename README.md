# Binance Data Analysis Tools

This repository contains C++ programs for fetching Binance klines data and performing technical analysis, specifically implementing a Pi Cycle Indicator.

## Project Structure

- `1-get-binance-klines.cpp`: Fetches 1-day klines data for BTCUSDT from Binance and stores it in an SQLite database.
- `2-pi-cycle-indicator.cpp`: Calculates and displays the Pi Cycle Indicator based on the klines data stored in the SQLite database.
- `projection/projection.cpp`: A C++ port of a Python script for Bitcoin price projection based on technical indicators.
- `json.hpp`: Header-only library for JSON parsing (nlohmann/json).

## Prerequisites

Before building, ensure you have the following installed:

- **C++ Compiler**: A C++11 compatible compiler (e.g., g++, clang++).
- **CMake**: Version 3.10 or higher.
- **curl**: C++ library for making HTTP requests.
- **SQLite3**: C library for SQLite database operations.

### macOS (using Homebrew)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake curl sqlite3
```

## Build Instructions

1.  **Clone the repository and navigate to the project root directory:**

    ```bash
    git clone git@github.com:luzbetak/binance-klines.git
    cd binance-klines # Or whatever you named the cloned directory
    ```

2.  **Create a build directory and navigate into it:**

    ```bash
    mkdir -p build
    cd build
    ```

3.  **Run CMake to configure the project:**

    ```bash
    cmake ..
    ```

4.  **Build the executables:**

    ```bash
    make
    ```

5.  **Install the executables (optional, but recommended to copy them to the project root):**

    ```bash
    make install
    ```

    This will copy `1-get-binance-klines` and `2-pi-cycle-indicator` to the project root directory.

## Running the Programs

### 1. Fetch Binance Klines Data

Before running the Pi Cycle Indicator, you need to populate the `binance.db` database with klines data. This program fetches 1-day klines for BTCUSDT.

```bash
./1-get-binance-klines
```

This will create (or update) the `binance.db` file in the project root directory.

### 2. Run Pi Cycle Indicator

This program calculates and displays the Pi Cycle Indicator based on the data in `binance.db`.

```bash
./2-pi-cycle-indicator [num_display_days]
```

- `[num_display_days]` (optional): The number of recent days to display in the table. Defaults to 33.

**Example:**

```bash
./2-pi-cycle-indicator 50
```

### 3. Run Bitcoin Price Projection

This program calculates a Bitcoin price projection.

```bash
./projection/projection
```

## Notes

- **API Keys**: No API keys are required for fetching public klines data from Binance.
- **SSL Verification**: SSL certificate verification is disabled in the cURL calls for simplicity in this example. This is **not recommended for production environments**.
- **Data Path**: The `binance.db` file is expected to be located in the project root directory. Ensure this directory exists and is writable.

## Possible ASCII Table C++ Libraries to Consider Using in the Future

1.  `tabulate` (C++):
    *   Description: This is probably the most direct equivalent to Python's tabulate or prettytable. It's a header-only library, which makes it
        incredibly easy to integrate into your project (just include the header file).
    *   Features: It handles column alignment, padding, various table styles (e.g., plain, grid, fancy_grid), and even colors.
    *   Ease of Use: Very straightforward API. You typically add data row by row or column by column, and then print the table.
    *   Example (conceptual):

        ```cpp
        #include "tabulate/table.hpp"
        using namespace tabulate;

        Table table;
        table.add_row({"Date", "Price", "Move", "Offset", "CEILING", "MEDIAN", "FLOOR", "Step", "Change", "52-weeks"});
        table.add_row({"2025-06-27", "107,000", "-0.40%", "6.0%", "118,363", "100,948", "83,533", "126", "-426", "75.20%"});
        // ... add more rows
        table.format()
            .font_align(FontAlign::right)
            .column(0).font_align(FontAlign::left); // Align first column left

        std::cout << table << std::endl;
        ```

    *   Integration: Since it's header-only, you just need to download table.hpp and include it. If using CMake, you can add it as a subdirectory
        or simply include its path.


2.  `cli-table`:
    *   Description: Another good option for command-line interface tables. It's also relatively easy to use and provides good formatting
        capabilities.
    *   Features: Supports various styles, borders, and cell formatting.
    *   Integration: Typically integrated via CMake.


3.  `cpp-terminal`:
    *   Description: This is a more comprehensive library for terminal manipulation (colors, cursor control, input, etc.), and it includes a
        module for table rendering. It's more heavyweight than tabulate if you only only need tables, but powerful if you plan more advanced CLI
        features.
