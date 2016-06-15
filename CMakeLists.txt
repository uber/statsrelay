cmake_minimum_required(VERSION 2.8)
project(statsrelay)
set(PACKAGE_VERSION "2.1.1")
set(PACKAGE_STRING "statsrelay ${PACKAGE_VERSION}")

enable_testing()

INCLUDE(CheckIncludeFile)
INCLUDE(CheckFunctionExists)
INCLUDE(CheckLibraryExists)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -O2 -fno-omit-frame-pointer -fno-strict-aliasing -g3")

CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_LIBRARY_EXISTS(ev ev_run "" HAVE_LIBEV)
CHECK_LIBRARY_EXISTS(jansson json_load_file "" HAVE_JANSSON)
CHECK_FUNCTION_EXISTS(flock HAVE_FLOCK)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h)
include_directories(${PROJECT_BINARY_DIR})

set(SOURCE_FILES
    src/buffer.c
    src/buffer.h
    src/filter.c
    src/filter.h
    src/hashlib.c
    src/hashlib.h
    src/hashmap.c
    src/hashmap.h
    src/hashring.c
    src/hashring.h
    src/json_config.c
    src/json_config.h
    src/list.c
    src/list.h
    src/log.c
    src/log.h
    src/pidfile.c
    src/pidfile.h
    src/protocol.c
    src/protocol.h
    src/server.c
    src/server.h
    src/stats.c
    src/stats.h
    src/tcpclient.c
    src/tcpclient.h
    src/tcpserver.c
    src/tcpserver.h
    src/udpserver.c
    src/udpserver.h
    src/validate.c
    src/validate.h
    src/vector.c
    src/vector.h
        src/sampling.h src/sampling.c)

add_executable(statsrelay ${SOURCE_FILES} src/main.c)
add_executable(stresstest src/stresstest.c)
add_executable(stathasher ${SOURCE_FILES} src/stathasher.c)

target_link_libraries(stathasher ev pcre jansson)
target_link_libraries(statsrelay ev pcre jansson)

add_executable(test_hashlib ${SOURCE_FILES} src/tests/test_hashlib.c)
target_link_libraries(test_hashlib ev pcre jansson)
add_test(NAME test_hashlib COMMAND test_hashlib)

add_executable(test_hashring ${SOURCE_FILES} src/tests/test_hashring.c)
target_link_libraries(test_hashring ev pcre jansson)
add_test(NAME test_hashring COMMAND test_hashring WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/tests)

add_executable(test_vector ${SOURCE_FILES} src/tests/test_vector.c)
target_link_libraries(test_vector ev pcre jansson)
add_test(NAME test_vector COMMAND test_vector)


add_executable(test_sampler ${SOURCE_FILES} src/tests/test_sampler.c)
target_link_libraries(test_sampler ev pcre jansson)
add_test(NAME test_sampler COMMAND test_sampler)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
        DEPENDS test_vector test_hashring test_hashlib)


add_custom_target(integ python ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_endtoend.py DEPENDS statsrelay stathasher WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

