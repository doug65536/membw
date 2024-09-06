CXXFLAGS=-g -std=c++17 -mavx2 -fno-exceptions
ifeq ($(DEBUG),)
CXXFLAGS+=-O2
endif
membw: membw.cc
	$(CXX) $(CXXFLAGS) -o $@ $^ -static
