cmake_minimum_required(VERSION 3.20)
project(ErintesGFX LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(nlohmann_json REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(erintesgfx src/main.cpp src/generator.cpp src/project.cpp src/canvas.cpp src/mainwindow.cpp)
target_link_libraries(erintesgfx PRIVATE nlohmann_json::nlohmann_json Qt6::Widgets)

# textconvert needs this next to the binary (see tools/ruby_compat.rb)
configure_file(tools/ruby_compat.rb ruby_compat.rb COPYONLY)

add_executable(textdb_check tests/textdb_check.cpp src/project.cpp)
target_link_libraries(textdb_check PRIVATE nlohmann_json::nlohmann_json Qt6::Widgets)
