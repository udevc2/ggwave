set(TARGET ggwave-from-pipe)

add_executable(${TARGET} main.cpp)

target_include_directories(${TARGET} PRIVATE
    ..
    ${SDL2_INCLUDE_DIRS}
    )

target_link_libraries(${TARGET} PRIVATE
    ggwave
    ggwave-common
    ggwave-common-sdl2
    ${SDL2_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    )


