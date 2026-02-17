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
    MKDIR := cmake -E make_directory
    CP := cmake -E copy
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
    MKDIR := mkdir -p
    CP := cp
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
	@$(MKDIR) $(DIST_DIR)/bin $(DIST_DIR)/lib $(DIST_DIR)/include $(DIST_DIR)/share/bebop
ifeq ($(OS),Windows_NT)
	@$(CP) $(BUILD_DIR)/bin/bebopc.exe $(DIST_DIR)/bin/
	@-$(CP) $(BUILD_DIR)/bin/bebopc-gen-*.exe $(DIST_DIR)/bin/
	@$(CP) $(BUILD_DIR)/lib/bebop.lib $(DIST_DIR)/lib/
else
	@$(CP) $(BUILD_DIR)/bin/bebopc $(DIST_DIR)/bin/
	@-$(CP) $(BUILD_DIR)/bin/bebopc-gen-* $(DIST_DIR)/bin/ 2>/dev/null
	@$(CP) $(BUILD_DIR)/lib/libbebop.a $(DIST_DIR)/lib/
endif
	@cmake -E copy_directory $(BUILD_DIR)/include $(DIST_DIR)/include
	@cmake -E copy_directory $(BUILD_DIR)/share/bebop $(DIST_DIR)/share/bebop

vscode:
	cd plugins/vscode && npm install && npm run compile && npm run package

archive:
	@tar -czf bebop-$(VERSION)-$(DETECTED_OS)-$(ARCH).tar.gz -C $(DIST_DIR) .

install: dist
	@$(MKDIR) $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include $(PREFIX)/share/bebop
	@$(CP) $(DIST_DIR)/bin/* $(PREFIX)/bin/
	@$(CP) $(DIST_DIR)/lib/* $(PREFIX)/lib/
	@cmake -E copy_directory $(DIST_DIR)/include $(PREFIX)/include
	@cmake -E copy_directory $(DIST_DIR)/share/bebop $(PREFIX)/share/bebop
	@echo "Installed bebop $(VERSION) to $(PREFIX)"

uninstall:
	@$(RM) $(PREFIX)/bin/bebopc $(PREFIX)/bin/bebopc-gen-*
ifeq ($(OS),Windows_NT)
	@$(RM) $(PREFIX)/lib/bebop.lib
else
	@$(RM) $(PREFIX)/lib/libbebop.a
endif
	@$(RM) $(PREFIX)/include/bebop
	@$(RM) $(PREFIX)/share/bebop
	@echo "Uninstalled bebop from $(PREFIX)"
