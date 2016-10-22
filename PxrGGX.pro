#-------------------------------------------------
#
# Project created by Declan Russell 2016-03-22T13:47:09
#
#-------------------------------------------------

QT       -= core gui

TARGET = PxrGGX
TEMPLATE = lib

OBJECTS_DIR = "obj"
MOC_DIR = "moc"

INCLUDEPATH += $$(RMANTREE)include  "$$PWD"/include

QMAKE_LIBDIR += $$(RMANTREE)\lib

LIBS += -llibprman

win32:DEFINES += WIN32
DEFINES += _USE_MATH_DEFINES

HEADERS += include/*.h

SOURCES += \
    src/PxrGGX.cpp

OTHER_FILES += *.args

win32:{
    QMAKE_CXXFLAGS += -nologo -MT
    INCLUDEPATH+=C:/boost
}
