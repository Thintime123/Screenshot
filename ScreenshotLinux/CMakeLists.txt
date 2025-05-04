cmake_minimum_required(VERSION 3.16)
project(ScreenshotLinux VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

add_executable(ScreenshotLinux
    main.cpp
    screenshotwindow.h
    screenshotwindow.cpp
)

target_link_libraries(ScreenshotLinux PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

install(TARGETS ScreenshotLinux
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)