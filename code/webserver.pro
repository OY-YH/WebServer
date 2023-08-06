TEMPLATE = app
CONFIG += console c++20
CONFIG -= app_bundle
CONFIG -= qt
LIBS += -L/usr/lib/ -lmariadb

SOURCES += \
    buffer.cpp \
        main.cpp

HEADERS += \
    buffer.h \


include($$PWD/noactive/noactive.pri)

include($$PWD/http/http.pri)

include($$PWD/server/server.pri)

include($$PWD/pool/pool.pri)

include($$PWD/Log/Log.pri)

