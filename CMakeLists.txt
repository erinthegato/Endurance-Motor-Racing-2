# Endurance Motor Racing 2.0 - PC edition (Windows / Linux): SDL2 + OpenGL 2.1.
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
#   -> build/EnduranceMotorRacing2(.exe); run it with DATA/ and HD/ next to it (the build copies them).
# Windows: MSYS2 MinGW64 with mingw-w64-x86_64-{gcc,cmake,SDL2,zlib}.
# The Vita build is vita/CMakeLists.txt; the software-rendered test build is Makefile.pc.
cmake_minimum_required(VERSION 3.16)
project(EnduranceMotorRacing2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

set(GAME_SRC
  src/data.cpp src/track.cpp src/race.cpp src/racecontrol.cpp src/launch.cpp src/reliability.cpp
  src/champ.cpp src/career.cpp src/settings.cpp src/menu.cpp
  src/sound.cpp src/render.cpp src/render_fx.cpp src/clip.cpp
  src/game.cpp src/intro.cpp src/trophies.cpp src/setups.cpp src/setupeng.cpp src/walk.cpp
  src/platform_desktop.cpp)

# WIN32: a normal Windows program (no console window); the log goes to %APPDATA%\CODE4\EnduranceMotorRacing2\log.txt
add_executable(EnduranceMotorRacing2 WIN32 ${GAME_SRC})
target_include_directories(EnduranceMotorRacing2 PRIVATE src)
target_compile_definitions(EnduranceMotorRacing2 PRIVATE PLATFORM_DESKTOP=1)
target_compile_options(EnduranceMotorRacing2 PRIVATE -O2 -Wall -fno-strict-aliasing)

# every fopen / remove goes through the path mapper in platform_desktop.cpp
target_link_options(EnduranceMotorRacing2 PRIVATE -Wl,--wrap=fopen,--wrap=remove)
if(MINGW)
  target_link_options(EnduranceMotorRacing2 PRIVATE -static-libgcc -static-libstdc++)
endif()

if(TARGET SDL2::SDL2main)
  target_link_libraries(EnduranceMotorRacing2 PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(EnduranceMotorRacing2 PRIVATE SDL2::SDL2)
else()
  target_include_directories(EnduranceMotorRacing2 PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(EnduranceMotorRacing2 PRIVATE ${SDL2_LIBRARIES})
endif()
target_link_libraries(EnduranceMotorRacing2 PRIVATE OpenGL::GL ZLIB::ZLIB)

# the game's files next to the program
add_custom_command(TARGET EnduranceMotorRacing2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/DATA $<TARGET_FILE_DIR:EnduranceMotorRacing2>/DATA
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/HD $<TARGET_FILE_DIR:EnduranceMotorRacing2>/HD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/PC
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/PC $<TARGET_FILE_DIR:EnduranceMotorRacing2>/PC)
