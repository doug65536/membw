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

help:
	@echo "Available targets:"
	@echo "  all            - Build for the current platform"
	@echo "  clean          - Remove built binaries"
	@echo "  run            - Build and/or run executable"
	@echo "  build-windows  - Cross-compile for Windows"
	@echo "  build-linux    - Cross-compile for Linux"
	@echo "  build-arm      - Cross-compile for ARM (e.g., Raspberry Pi)"
	@echo "  build-all      - Build for all platforms"

membw.o: membw.cc
	$(CXX) $(CXXFLAGS) -MMD -MP -o $@ -c $^

membw$(EXE_EXT): membw.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -static

-include membw.d

clean:
	rm -f membw$(EXE_EXT) membw.d

run: ./membw$(EXE_EXT)
	./membw$(EXE_EXT)

build-windows:
	$(MAKE) CXX=x86_64-w64-mingw32-g++ -B

build-linux:
	$(MAKE) CXX=x86_64-linux-gnu-g++ -B

build-arm:
	$(MAKE) CXX=aarch64-linux-gnu-g++ -B

build-all: build-windows build-linux build-arm

.PHONY: help clean build-windows build-linux build-arm
