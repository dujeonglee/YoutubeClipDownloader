.PHONY : all debug release
.SUFFIXES : .cpp .o

SOURCES  := $(wildcard *.cpp)
INCLUDES := 
OBJECTS  := $(SOURCES:.cpp=.o)
LIBRARY := -lcurl
CPP := g++
TARGET = ytdl

all : debug

$(TARGET) : $(OBJECTS)
	$(CPP) -o $@  $^ $(LIBRARY)

.cpp.o : $(SOURCES)
	$(CPP) $(CPPFLAGS) $(INCLUDES) $(SOURCES) $(LIBRARY)

clean :
	rm -rf $(OBJECTS) $(TARGET) *~

debug : CPPFLAGS := -g -c -Wall -Werror -std=c++11
debug : $(TARGET)
	./ytdl rn-wj4pRpIE

release : CPPFLAGS := -O0 -c -Wall -Werror -std=c++11
release : $(TARGET)
	./ytdl rn-wj4pRpIE
