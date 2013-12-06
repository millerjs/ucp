C++ = g++

DIR = $(shell pwd)

CCFLAGS = -Wno-write-strings

APP = ucp
APPOUT = ucp

all: $(APP) 

%.o: %.cpp 
	$(C++) $(CCFLAGS) $< -c

ucp: handler.o files.o
	$(C++) $^ -o $(APPOUT) $(LDFLAGS)

clean:
	rm -f *.o $(APPOUT) 

install:
	export PATH=$(DIR):$$PATH
