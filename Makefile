SHELL := /usr/bin/env bash

BUILD_DIR ?= tmp/build
NO_SMB_BUILD_DIR ?= tmp/build-no-smb
PACKAGE_BUILD_DIR ?= tmp/package-linux
SAMBA_BUILD_DIR ?= tmp/build-samba
GENERATOR ?= Ninja
BUILD_TYPE ?= Debug
PACKAGE_BUILD_TYPE ?= Release
CTEST_ARGS ?= --output-on-failure
APP := $(BUILD_DIR)/src/app/smb-browser
JOBS ?= --parallel
DOCKER_COMPOSE ?= docker compose
SAMBA_COMPOSE_FILE := tests/integration/samba/docker-compose.yml

.DEFAULT_GOAL := help

.PHONY: help
help:
	@printf '%s\n' 'SMB Browser developer commands'
	@printf '\n%s\n' 'Common:'
	@printf '  %-22s %s\n' 'make setup' 'Install Ubuntu 22.04 build dependencies'
	@printf '  %-22s %s\n' 'make configure' 'Configure default build with libsmb2 backend'
	@printf '  %-22s %s\n' 'make build' 'Build default build directory'
	@printf '  %-22s %s\n' 'make test' 'Run default CTest suite'
	@printf '  %-22s %s\n' 'make run' 'Run the desktop app'
	@printf '  %-22s %s\n' 'make ci' 'Configure, build and test default profile'
	@printf '\n%s\n' 'Packaging:'
	@printf '  %-22s %s\n' 'make package-linux' 'Build Linux DEB package'
	@printf '  %-22s %s\n' 'make smoke-linux' 'Smoke-test generated Linux package'
	@printf '  %-22s %s\n' 'make install' 'Install from BUILD_DIR using cmake --install'
	@printf '\n%s\n' 'Alternative profiles:'
	@printf '  %-22s %s\n' 'make no-smb' 'Configure/build/test without libsmb2 backend'
	@printf '  %-22s %s\n' 'make libsmb2' 'Build libsmb2 into tmp/libsmb2-prefix manually'
	@printf '  %-22s %s\n' 'make samba-up' 'Start Docker Samba fixture'
	@printf '  %-22s %s\n' 'make samba-test' 'Run opt-in Docker Samba integration tests'
	@printf '  %-22s %s\n' 'make samba-down' 'Stop Docker Samba fixture'
	@printf '\n%s\n' 'Maintenance:'
	@printf '  %-22s %s\n' 'make status' 'Show git status'
	@printf '  %-22s %s\n' 'make clean' 'Remove common build/package directories under tmp'
	@printf '  %-22s %s\n' 'make distclean' 'Remove all tmp outputs'

.PHONY: setup
setup:
	sudo apt-get update
	sudo apt-get install -y --no-install-recommends \
		build-essential \
		cmake \
		git \
		ninja-build \
		pkg-config \
		qtbase5-dev \
		libqt5svg5-dev \
		qttools5-dev-tools \
		qtkeychain-qt5-dev \
		libsodium-dev \
		smbclient

.PHONY: configure
configure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

.PHONY: build
build: configure
	cmake --build $(BUILD_DIR) $(JOBS)

.PHONY: test
test: build
	ctest --test-dir $(BUILD_DIR) $(CTEST_ARGS)

.PHONY: ci
ci: test

.PHONY: run
run: build
	$(APP)

.PHONY: run-offscreen
run-offscreen: build
	QT_QPA_PLATFORM=offscreen timeout 3 $(APP) || test $$? -eq 124

.PHONY: package-linux
package-linux:
	cmake -S . -B $(PACKAGE_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(PACKAGE_BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=ON \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
	cmake --build $(PACKAGE_BUILD_DIR) --target package $(JOBS)

.PHONY: smoke-linux
smoke-linux: package-linux
	scripts/package-smoke-linux.sh

.PHONY: install
install: build
	cmake --install $(BUILD_DIR)

.PHONY: no-smb-configure
no-smb-configure:
	cmake -S . -B $(NO_SMB_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

.PHONY: no-smb
no-smb: no-smb-configure
	cmake --build $(NO_SMB_BUILD_DIR) $(JOBS)
	ctest --test-dir $(NO_SMB_BUILD_DIR) $(CTEST_ARGS)

.PHONY: libsmb2
libsmb2:
	scripts/build-libsmb2.sh

.PHONY: samba-up
samba-up:
	$(DOCKER_COMPOSE) -f $(SAMBA_COMPOSE_FILE) up -d --build

.PHONY: samba-configure
samba-configure:
	cmake -S . -B $(SAMBA_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=ON

.PHONY: samba-test
samba-test: samba-configure
	cmake --build $(SAMBA_BUILD_DIR) $(JOBS)
	ctest --test-dir $(SAMBA_BUILD_DIR) -L docker-samba $(CTEST_ARGS)

.PHONY: samba-down
samba-down:
	$(DOCKER_COMPOSE) -f $(SAMBA_COMPOSE_FILE) down -v

.PHONY: status
status:
	git status --short

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(NO_SMB_BUILD_DIR) $(PACKAGE_BUILD_DIR) $(SAMBA_BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf tmp
