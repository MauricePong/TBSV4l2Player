TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

TARGET = tbsplayer
INCLUDEPATH += .
DESTDIR     = $$PWD/../bin
CONFIG      += qt warn_off
SOURCES += \
        main.c  video.c \
    get_media_devices.c


HEADERS += \
        video.h \
    get_media_devices.h


INCLUDEPATH +=  /home/pzw/Desktop/OtherLib/src/include
INCLUDEPATH +=  $$PWD/src

LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libavformat.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libavdevice.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libavcodec.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libavfilter.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libavutil.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libpostproc.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libswresample.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libswscale.a
LIBS  +=/lib64/libSDL2.a
LIBS  +=/lib64/libSDL2_test.a
LIBS  +=/lib64/libSDL2main.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libfdk-aac.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libz.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libbz2.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libx264.a
LIBS +=/home/pzw/Desktop/OtherLib/src/lib/liblzma.a
#LIBS +=/home/pzw/Desktop/OtherLib/src/lib/libasound.a
LIBS += -lasound -lSDL
