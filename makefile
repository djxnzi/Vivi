.PHONY: all build run clean help
CXX		:= g++
FLAGS  	:= -std=c++17 -Wall -Wextra -Isrc
#MAKEFLAGS += --silent
SRCS   	:= main.cpp

# Set default option
BUILD   ?= debug

ifeq ($(BUILD),debug)
	FLAGS += -ggdb3 -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
else ifeq ($(BUILD),release)
	FLAGS += -O2 -flto -march=native
else
	$(info Unknown BUILD=$(BUILD); use debug or release)
endif

all: build/vivi

clean:
	rm -rf build

build:
	mkdir -p build

build/vivi: build $(SRCS)
	$(CXX) -o $@ $(SRCS) $(FLAGS)

help:
	@echo "Usage:"
	@echo "  make               - Build main program with default flags"
	@echo "  make BUILD=debug   - Build with debug flags"
	@echo "  make BUILD=release - Build with release flags"
	@echo "  make clean         - Remove build artifacts"
