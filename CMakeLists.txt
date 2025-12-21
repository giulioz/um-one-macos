
cmake_minimum_required(VERSION 3.5)

# Project name & version (MAJOR.MINOR-RELTYPE)
project (UMOne)

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set (CMAKE_CXX_STANDARD 17)

find_package(LibUSB REQUIRED)
message(STATUS "LibUSB include directory is ${LibUSB_INCLUDE_DIRS}")

find_package(PortMidi REQUIRED)
message(STATUS "PortMIDI include directory is ${PORTMIDI_INCLUDE_DIR}")

add_executable(driver driver.cpp)

target_include_directories(driver PUBLIC ${LibUSB_INCLUDE_DIRS} ${PORTMIDI_INCLUDE_DIR})
target_link_libraries(driver ${LibUSB_LIBRARIES} ${PORTMIDI_LIBRARIES})
