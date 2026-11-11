# SPDX-License-Identifier: GPL-2.0-or-later
# Keep build rules and dependency discovery in CMake.
# GNU Make 4.2 introduced the $(file <...) cache read used below.
ifneq ($(filter 0.% 1.% 2.% 3.% 4.0 4.0.% 4.1 4.1.%,$(MAKE_VERSION)),)
$(error GNU Make 4.2 or newer is required; found $(MAKE_VERSION))
endif

.DEFAULT_GOAL := build

CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build
# Reuse the configured type; command-line/environment overrides still win.
BUILD_TYPE ?= $(or $(word 2,$(subst =, ,$(filter CMAKE_BUILD_TYPE:%,$(file <$(BUILD_DIR)/CMakeCache.txt)))),Release)
JOBS ?= 2
CMAKE_ARGS ?=
CTEST_ARGS ?=

.PHONY: all configure build test clean help

all: build

configure:
	"$(CMAKE)" -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_ARGS)

build: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel $(JOBS)

test: build
	"$(CTEST)" --test-dir "$(BUILD_DIR)" -C "$(BUILD_TYPE)" --output-on-failure $(CTEST_ARGS) --no-tests=error

# Use CMake's clean target; retain the configuration and downloaded dependencies.
clean:
	"$(CMAKE)" --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --target clean

help:
	@echo "Requires GNU Make 4.2 or newer"
	@echo "make           Configure and build BUI (cached type, otherwise Release)"
	@echo "make configure Configure without building"
	@echo "make test      Build and run the CTest suites"
	@echo "make clean     Clean a configured build, keeping its CMake cache"
	@echo "Options: BUILD_DIR=build BUILD_TYPE=Release JOBS=2"
	@echo "Extra flags: CMAKE_ARGS and CTEST_ARGS (see docs/getting-started.md)"
