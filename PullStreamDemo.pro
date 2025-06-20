QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += $$PWD/include \
    $$PWD/include/FFmpeg \
    $$PWD/LogDemo \
    $$PWD/Pull

win32:CONFIG(release, debug|release): DESTDIR += $$PWD/bin/Release
else:win32:CONFIG(debug, debug|release): DESTDIR += $$PWD/bin/Debug

SOURCES += \
    Pull/audiodecodethread.cpp \
    Pull/rtspsyncpull.cpp \
    Pull/streampullthread.cpp \
    Pull/videodecodethread.cpp \
    Pull/playimage.cpp \
    Pull/audioplayer.cpp \
#    ffmpegdecode.cpp \
#    ffmpegthread.cpp \
    main.cpp \
    mainwindow.cpp



HEADERS += \
    DataStruct.h \
    Pull/audiodecodethread.h \
    Pull/rtspsyncpull.h \
    Pull/streampullthread.h \
    Pull/videodecodethread.h \
    Pull/playimage.h \
    Pull/audioplayer.h \
#    ffmpegdecode.h \
#    ffmpegthread.h \
    mainwindow.h \

FORMS += \
    mainwindow.ui

# msvc >= 2017  编译器使用utf-8编码
msvc {
    greaterThan(QMAKE_MSC_VER, 1900){
        QMAKE_CFLAGS += /utf-8
        QMAKE_CXXFLAGS += /utf-8
    }
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DEPENDPATH += $$PWD/lib \
              $$PWD/lib/FFmpeg \

LIBS += -L$$PWD/lib/FFmpeg/ -lavcodec -lavfilter -lavformat -lswscale -lavutil -lswresample -lavdevice


