.PHONY: all build run clean help install uninstall
CXX		:= g++
FLAGS  	:= -std=c++17 -Wall -Wextra -Isrc
MAKEFLAGS += --silent
SRCS   	:= main.cpp
SHELL	:= /bin/bash

# Set default option
BUILD   ?= release

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

install:
	@sudo -k
	@sudo -v || exit 1

	@echo "Building Vivi..."
	@TIMEFORMAT="Vivi built in %3R seconds"; time $(MAKE) build/vivi
	@echo

	@echo "Installing Vivi..."
	@sudo install -Dm755 build/vivi /usr/bin/vivi
	@sudo install -d /usr/lib/vivi/core
	@sudo cp -a lib/core/. /usr/lib/vivi/core/

	@cmp -s build/vivi /usr/bin/vivi || { echo "Installation failed"; exit 1; }
	@echo "Vivi installed successfully."
	@echo
	@sudo vivi --version

uninstall:
	@read -p "Are you sure you want to uninstall vivi? [y/N] " confirm; \
	if [ "$$confirm" = "y" ] || [ "$$confirm" = "Y" ]; then \
		sudo rm -f /usr/bin/vivi; \
		sudo rm -rf /usr/lib/vivi; \
		echo "vivi uninstalled."; \
	fi

build/vivi: build $(SRCS)
	$(CXX) -o $@ $(SRCS) $(FLAGS)

help:
	@echo "Usage:"
	@echo "  make               - Build main program with default flags"
	@echo "  make BUILD=debug   - Build with debug flags"
	@echo "  make BUILD=release - Build with release flags"
	@echo "  make clean         - Remove build artifacts"
	@echo "  make install		- Installs Vivi to /usr/bin and /usr/lib"
	@echo "  make uninstall		- Uninstalls Vivi from the system"