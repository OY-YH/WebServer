TEMPLATE = app
CONFIG += console c++20
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        http_conn.cpp \
        locker.cpp \
        main.cpp

HEADERS += \
    http_conn.h \
    locker.h \
    threadpool.h

