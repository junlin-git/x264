HEADERS += \
    include/x264.h \
    include/x264_config.h

SOURCES += \
    main.c

LIBS += -L$$PWD/lib/ -lx264  -lv4l2 -ldl

INCLUDEPATH += $$PWD/include
DEPENDPATH += $$PWD/include
