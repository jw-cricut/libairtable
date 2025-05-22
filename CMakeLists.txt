cmake_minimum_required(VERSION 3.10)

# Project setup
project(airtable)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

# Help CMake find Homebrew libraries
list(INSERT CMAKE_SYSTEM_PREFIX_PATH 0 /opt/homebrew)

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(fmt REQUIRED)
find_package(phosg REQUIRED)

# ASIO is header-only, so we just locate its headers
find_path(ASIO_INCLUDE_DIR asio.hpp PATH_SUFFIXES asio)

# Source files
add_library(airtable
    src/AirtableClient.cc
    src/AsyncHTTPClient.cc
    src/AsyncUtils.cc
    src/FieldTypes.cc
)

# Includes
target_include_directories(airtable PUBLIC
    ${ASIO_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(airtable
    phosg
    OpenSSL::SSL
    OpenSSL::Crypto
    fmt::fmt
)

# Executable
add_executable(airtable-cli src/AirtableCLI.cc)
target_link_libraries(airtable-cli airtable)

# Installation configuration
file(GLOB Headers ${CMAKE_SOURCE_DIR}/src/*.hh)
install(TARGETS airtable DESTINATION lib)
install(TARGETS airtable-cli DESTINATION bin)
install(FILES ${Headers} DESTINATION include/airtable)
