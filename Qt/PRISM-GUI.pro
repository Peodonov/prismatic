#-------------------------------------------------
#
# Project created by QtCreator 2017-03-02T07:59:27
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = PRISM-GUI
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

#hardcoded!
DEFINES += PRISM_ENABLE_GPU=0

SOURCES += main.cpp\
        prismmainwindow.cpp \ 
    ../src/configure.cpp \
    ../src/getWorkID.cpp \
    ../src/Multislice_entry.cpp \
    ../src/Multislice.cpp \
    ../src/PRISM_entry.cpp \
    ../src/PRISM01.cpp \
    ../src/PRISM02.cpp \
    ../src/PRISM03.cpp \
    ../src/projectedPotential.cpp \
    ../src/prism_qthreads.cpp
LIBS +=  -lfftw3 -lfftw3f -lfftw3_threads -lfftw3f_threads -L/usr/local/lib  
HEADERS  += prismmainwindow.h \
    ../include/prism_qthreads.h
QMAKE_CXXFLAGS += -std=c++11
FORMS    += \
    prismmainwindow.ui
INCLUDEPATH += ../include \
/usr/local/include
INCLUDEPATH += ../
INCLUDEPATH += ../include/boost/
#INCLUDEPATH += /usr/local/include /Developer/NVIDIA/CUDA-8.0/include/
QMAKE_LFLAGS += -F /Users/ajpryor/Documents/MATLAB/multislice/PRISM/Qt
#LIBS += -lprism_shared -L.

#DISTFILES += \
#    ../src/Multislice.cu \
#    ../src/PRISM02.cu \
#    ../src/PRISM03.cu \
#    ../src/utility.cu
