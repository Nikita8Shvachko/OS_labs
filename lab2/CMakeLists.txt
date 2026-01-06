cmake_minimum_required(VERSION 3.15)
project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wall -Werror)

file(GLOB CONN_SOURCES "conn_*.cpp")
set(COMMON_SOURCES ipc_utils.cpp)

foreach(CONN_SRC ${CONN_SOURCES})
    get_filename_component(CONN_NAME ${CONN_SRC} NAME_WE)
    string(REPLACE "conn_" "" TYPE_CODE ${CONN_NAME})

    set(HOST_TARGET host_${TYPE_CODE})
    set(CLIENT_TARGET client_${TYPE_CODE})

    add_executable(${HOST_TARGET} host.cpp ${CONN_SRC} ${COMMON_SOURCES})
    add_executable(${CLIENT_TARGET} client.cpp ${CONN_SRC} ${COMMON_SOURCES})

    target_compile_definitions(${HOST_TARGET} PRIVATE CONN_TYPE_CODE="${TYPE_CODE}")
    target_compile_definitions(${CLIENT_TARGET} PRIVATE CONN_TYPE_CODE="${TYPE_CODE}")

    if(APPLE)
        target_link_libraries(${HOST_TARGET} PRIVATE pthread)
        target_link_libraries(${CLIENT_TARGET} PRIVATE pthread)
    else()
        target_link_libraries(${HOST_TARGET} PRIVATE pthread rt)
        target_link_libraries(${CLIENT_TARGET} PRIVATE pthread rt)
    endif()
endforeach()

