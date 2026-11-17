BUILD_DIR := build
CMAKE_DIR := $(BUILD_DIR)/cmake
DIST_DIR := dist

ifeq ($(OS),Windows_NT)
    ifneq ($(MSYSTEM),)
        VERSION := $(shell cat VERSION)
    else
        VERSION := $(shell type VERSION)
    endif
    DETECTED_OS := windows
    ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
        ARCH := arm64
    else ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
        ARCH := x64
    else
        ARCH := x86
    endif
    JOBS ?= $(NUMBER_OF_PROCESSORS)
    RM := cmake -E rm -rf
    CTEST_CONFIG := -C Debug
else
    VERSION := $(shell cat VERSION)
    UNAME_S := $(shell uname -s)
    UNAME_M := $(shell uname -m)
    ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := darwin
    else
        DETECTED_OS := linux
    endif
    ARCH := $(UNAME_M)
    JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    RM := rm -rf
    CTEST_CONFIG :=
endif

VERSION_BASE := $(firstword $(subst -, ,$(VERSION)))
VERSION_MAJOR := $(word 1,$(subst ., ,$(VERSION_BASE)))
VERSION_MINOR := $(word 2,$(subst ., ,$(VERSION_BASE)))
VERSION_PATCH := $(word 3,$(subst ., ,$(VERSION_BASE)))
VERSION_SUFFIX := $(word 2,$(subst -, ,$(VERSION)))

PREFIX ?= $(HOME)/.local

.PHONY: all debug release test clean publish dist vscode archive install uninstall

all: release

debug:
	@cmake -B $(CMAKE_DIR) -DCMAKE_BUILD_TYPE=Debug \
		-DBEBOP_VERSION_MAJOR=$(VERSION_MAJOR) \
		-DBEBOP_VERSION_MINOR=$(VERSION_MINOR) \
		-DBEBOP_VERSION_PATCH=$(VERSION_PATCH) \
		-DBEBOP_VERSION_SUFFIX=$(VERSION_SUFFIX)
ifeq ($(OS),Windows_NT)
	@cmake --build $(CMAKE_DIR) -j$(JOBS) -- /nodeReuse:false
else
	@cmake --build $(CMAKE_DIR) -j$(JOBS)
endif

release:
	@cmake -B $(CMAKE_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DBEBOP_VERSION_MAJOR=$(VERSION_MAJOR) \
		-DBEBOP_VERSION_MINOR=$(VERSION_MINOR) \
		-DBEBOP_VERSION_PATCH=$(VERSION_PATCH) \
		-DBEBOP_VERSION_SUFFIX=$(VERSION_SUFFIX)
ifeq ($(OS),Windows_NT)
	@cmake --build $(CMAKE_DIR) -j$(JOBS) -- /nodeReuse:false
else
	@cmake --build $(CMAKE_DIR) -j$(JOBS)
endif

test: debug
	@ctest --test-dir $(CMAKE_DIR) $(CTEST_CONFIG) --output-on-failure

clean:
	$(RM) $(BUILD_DIR) $(DIST_DIR)

publish: release dist archive
	@echo "Published bebop-$(VERSION)-$(DETECTED_OS)-$(ARCH).tar.gz"

dist:
	@$(RM) $(DIST_DIR)
	@cmake --install $(CMAKE_DIR) --prefix "$(abspath $(DIST_DIR))" --config Release

vscode:
	cd plugins/vscode && npm pkg set version=$(VERSION_BASE) && npm install && npm run compile && npm run package

archive:
	@tar -czf bebop-$(VERSION)-$(DETECTED_OS)-$(ARCH).tar.gz -C $(DIST_DIR) .

install:
	@cmake --install $(CMAKE_DIR) --prefix "$(PREFIX)" --config Release
	@echo "Installed bebop $(VERSION) to $(PREFIX)"

uninstall:
	@$(RM) $(PREFIX)/bin/bebopc $(PREFIX)/bin/bebopc-gen-*
ifeq ($(OS),Windows_NT)
	@$(RM) $(PREFIX)/lib/bebop.lib
else
	@$(RM) $(PREFIX)/lib/libbebop.a
endif
	@$(RM) $(PREFIX)/include/bebop
	@$(RM) $(PREFIX)/include/bebop.h $(PREFIX)/include/bebop_wire.h $(PREFIX)/include/bebop_wire_codegen.h $(PREFIX)/include/bebop.hpp
	@$(RM) $(PREFIX)/lib/cmake/bebop-cpp
	@$(RM) $(PREFIX)/share/bebop
	@echo "Uninstalled bebop from $(PREFIX)"
