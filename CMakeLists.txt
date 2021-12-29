cmake_minimum_required(VERSION 3.10)



# Project setup

project(airtable)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

include_directories("/usr/local/include")
link_directories("/usr/local/lib")
list(INSERT CMAKE_SYSTEM_PREFIX_PATH 0 /opt/homebrew)



# Library and executable definitions

find_package(OpenSSL REQUIRED)

find_path     (LIBEVENT_INCLUDE_DIR NAMES event.h)
find_library  (LIBEVENT_LIBRARY     NAMES event)
find_library  (LIBEVENT_CORE        NAMES event_core)
find_library  (LIBEVENT_THREAD      NAMES event_pthreads)
find_library  (LIBEVENT_SSL         NAMES event_openssl)
set (LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
set (LIBEVENT_LIBRARIES
        ${LIBEVENT_LIBRARY}
        ${LIBEVENT_CORE}
        ${LIBEVENT_THREAD}
        ${LIBEVENT_SSL})

add_library(airtable src/AirtableClient.cc src/FieldTypes.cc)
target_include_directories(airtable PUBLIC ${LIBEVENT_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
target_link_libraries(airtable phosg event-async http-async ${LIBEVENT_LIBRARIES} ${OPENSSL_LIBRARIES})

add_executable(airtable-cli src/AirtableCLI.cc)
target_link_libraries(airtable-cli airtable)



# Installation configuration

file(GLOB Headers ${CMAKE_SOURCE_DIR}/src/*.hh)
install(TARGETS airtable DESTINATION lib)
install(TARGETS airtable-cli DESTINATION bin)
install(FILES ${Headers} DESTINATION include/airtable)
