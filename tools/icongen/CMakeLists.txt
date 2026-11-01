# Developer-only tool: renders resources/icons/app.svg into PNGs and a
# multi-size .ico. Not part of the application build; run tools/make-icons.ps1.
cmake_minimum_required(VERSION 3.21)
project(icongen LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Svg)
qt_standard_project_setup()
qt_add_executable(icongen main.cpp)
target_link_libraries(icongen PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
