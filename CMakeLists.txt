cmake_minimum_required(VERSION 3.16)
project(multiRecordGdiDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.14 REQUIRED COMPONENTS
    Core Gui Widgets Quick Qml WebEngine WebChannel
)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/WebViewWindow.cpp
    src/WindowLayout.cpp
    src/LayoutSolver.cpp
    src/GdiCompositeCapturer.cpp
    src/CompositeRecorder.cpp
    resources/resources.qrc
)

add_executable(multiRecordGdiDemo WIN32 ${SOURCES})

target_include_directories(multiRecordGdiDemo PRIVATE src)

target_link_libraries(multiRecordGdiDemo PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Quick
    Qt5::Qml
    Qt5::WebEngine
    Qt5::WebChannel
    dwmapi
    vfw32
)

if(WIN32)
    set_target_properties(multiRecordGdiDemo PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
