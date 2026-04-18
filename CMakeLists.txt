cmake_minimum_required(VERSION 3.16)
project(MediaPreviewClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{QT_ROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Concurrent)

add_executable(MediaPreviewClient
    main.cpp
    mainwindow.h
    mainwindow.cpp
    mainwindow_ui.cpp
    mainwindow_interaction.cpp
    mainwindow_media.cpp
    mainwindow_thumbnails.cpp
    resources.qrc
    app_icon.rc
)

target_link_libraries(MediaPreviewClient PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
)

if(WIN32)
    set_target_properties(MediaPreviewClient PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
