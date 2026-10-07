QT += core gui widgets network

TARGET = ScopeAgent
TEMPLATE = app
CONFIG += c++14
DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += src drivers

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/waveformwidget.cpp \
    src/agentclient.cpp \
    src/deviceconfig.cpp \
    drivers/virtualscopedriver.cpp \
    drivers/scpidriver.cpp

HEADERS += \
    src/mainwindow.h \
    src/waveformwidget.h \
    src/agentclient.h \
    src/deviceconfig.h \
    drivers/virtualscopedriver.h \
    drivers/scpidriver.h

# 高 DPI 与 UTF-8 源文件
win32:QMAKE_CXXFLAGS += -utf-8
