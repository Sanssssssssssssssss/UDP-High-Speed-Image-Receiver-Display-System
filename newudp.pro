QT += core gui widgets concurrent network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
win32-g++: QMAKE_CXXFLAGS += -fopenmp
win32-g++: LIBS += -fopenmp

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    src/app/ApplicationLauncher.cpp \
    src/network/UdpReceiver.cpp \
    src/processing/UdpFramePipelineWorker.cpp \
    src/processing/UdpFrameProcessor.cpp \
    src/processing/VideoRecorderWorker.cpp \
    src/ui/ControlUI.cpp

HEADERS += \
    include/app/ApplicationLauncher.h \
    include/network/UdpReceiver.h \
    include/processing/UdpFramePipelineWorker.h \
    include/processing/UdpFrameProcessor.h \
    include/processing/VideoRecorderWorker.h \
    include/ui/ControlUI.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

LIBS += -lWs2_32

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/include/app \
    $$PWD/include/inference \
    $$PWD/include/network \
    $$PWD/include/processing \
    $$PWD/include/ui

OPENCV_ROOT = $$(OPENCV_ROOT)
isEmpty(OPENCV_ROOT): OPENCV_ROOT = D:/OpenCV-MinGW-1

INCLUDEPATH += $$OPENCV_ROOT/include

LIBS += -L$$OPENCV_ROOT/x64/mingw/lib
LIBS += -lopencv_core348 \
        -lopencv_imgproc348 \
        -lopencv_highgui348 \
        -lopencv_imgcodecs348 \
        -lopencv_videoio348
