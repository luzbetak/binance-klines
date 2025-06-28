cmake_minimum_required(VERSION 3.10)
project(BinanceKlineFetcher CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find cURL
find_package(CURL REQUIRED)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Add the JSON header-only library
# Assuming json.hpp is in the same directory as the source file
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1)

add_executable(3-pi-cycle-pro 3-pi-cycle-pro.cpp)

target_link_libraries(3-pi-cycle-pro PRIVATE ${CURL_LIBRARIES} ${SQLite3_LIBRARIES})

# Install rules
install(TARGETS 3-pi-cycle-pro DESTINATION /Users/mac/binance-klines)

