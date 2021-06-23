cmake_minimum_required(VERSION 3.0)

project(YouTubeLiveStreamer)

if(DEFINED ENV{SNAPCRAFT_BUILD_ENVIRONMENT})
    add_definitions(-DSNAPCRAFT_BUILD=1)
    set(SNAPCRAFT_BUILD YES)
endif()

add_subdirectory(CxxPtr)

find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(SPDLOG REQUIRED spdlog)
pkg_search_module(LIBCONFIG REQUIRED libconfig)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.h,
    *.cmake,
    *.conf.sample)

file(GLOB_RECURSE SNAP RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    snap/*)

add_executable(${PROJECT_NAME} ${SOURCES} ${SNAP})
target_include_directories(${PROJECT_NAME} PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${LIBCONFIG_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    ${GLIB_LDFLAGS}
    ${SPDLOG_LDFLAGS}
    ${LIBCONFIG_LDFLAGS}
    ${GSTREAMER_LDFLAGS}
    CxxPtr
    Threads::Threads)

if(SNAPCRAFT_BUILD)
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/live-streamer.conf.sample DESTINATION etc)
endif()
