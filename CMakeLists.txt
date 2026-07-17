cmake_minimum_required(VERSION 3.10.0)
project(KemonoSearchC VERSION 0.1.0 LANGUAGES C)

set(CMAKE_BUILD_TYPE Debug)

find_package(CURL)

add_executable(KemonoSearchC main.c)


target_link_libraries(KemonoSearchC PRIVATE CURL::libcurl)
