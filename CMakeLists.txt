cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(QDBMP_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/qdbmp.c
)

SET(QDBMP_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(qdbmp
        "${QDBMP_SRC}"
        ""
        []
        ""
        "${QDBMP_INC}"
        ""
        "1")
