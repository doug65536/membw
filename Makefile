CXXFLAGS=-g -std=c++17 -fno-exceptions
ifeq ($(DEBUG),)
CXXFLAGS+=-O2
endif
CXXFLAGS += $(VECFLAGS)

ifneq (,$(findstring mingw32,$(CXX)))
    EXE_EXT := .exe
else ifdef WINDIR
    EXE_EXT := .exe
else
    EXE_EXT :=
endif

ifeq (,$(findstring aarch64,$(CXX)))
VECFLAGS ?= -march=native
else
VECFLAGS ?= -march=armv8-a -mtune=cortex-a72
endif

all: membw$(EXE_EXT)

membw$(EXE_EXT): membw.cc
	$(CXX) $(CXXFLAGS) -o $@ $^ -static

clean:
	rm -f membw$(EXE_EXT)

run: ./membw$(EXE_EXT)
	./membw$(EXE_EXT)

build-windows:
	$(MAKE) CXX=x86_64-w64-mingw32-g++ -B

build-linux:
	$(MAKE) CXX=x86_64-linux-gnu-g++ -B

build-arm:
	$(MAKE) -B \
		CXX=aarch64-linux-gnu-g++

build-all: build-windows build-linux build-arm

.PHONY: clean build-windows build-linux build-arm
