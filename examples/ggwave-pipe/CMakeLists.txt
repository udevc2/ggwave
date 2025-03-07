add_executable(ggwave-pipe main.cpp)

target_include_directories(ggwave-pipe PRIVATE
    ..
    ${SDL2_INCLUDE_DIRS}
    )

target_link_libraries(ggwave-pipe PRIVATE
    ggwave
    ggwave-common
    ggwave-common-sdl2
    ${CMAKE_THREAD_LIBS_INIT}
    )
