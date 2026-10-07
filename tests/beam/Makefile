# Unit tests for Beam's additions to the fork (no Qt, window or stream needed)
#   make -C tests/beam test

ROOT := ../..
CXXFLAGS += -std=c++17 -Wall -Wextra -g -I$(ROOT)/app -I$(ROOT)/moonlight-common-c/moonlight-common-c/src $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)

test_beammenu: test_beammenu.cpp $(ROOT)/app/streaming/beammenu.cpp $(ROOT)/app/streaming/beammenu.h
	$(CXX) $(CXXFLAGS) -o $@ test_beammenu.cpp $(ROOT)/app/streaming/beammenu.cpp $(LDLIBS)

test: test_beammenu
	./test_beammenu

clean:
	rm -f test_beammenu

.PHONY: test clean
