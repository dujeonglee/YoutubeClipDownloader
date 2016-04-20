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

debug : CPPFLAGS := -g -c -Wall -Werror
debug : $(TARGET)
	./ytdl https://www.youtube.com/watch?v=rn-wj4pRpIE mp4

release : CPPFLAGS := -O0 -c -Wall -Werror
release : $(TARGET)
	./ytdl https://www.youtube.com/watch?v=rn-wj4pRpIE mp4
