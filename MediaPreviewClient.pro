QT += core gui widgets multimedia multimediawidgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    mainwindow_ui.cpp \
    mainwindow_interaction.cpp \
    mainwindow_media.cpp \
    mainwindow_thumbnails.cpp

HEADERS += \
    mainwindow.h

RESOURCES += \
    resources.qrc

win32:RC_FILE += app_icon.rc

TARGET = MediaPreviewClient
TEMPLATE = app
