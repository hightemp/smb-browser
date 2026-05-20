SHELL := /usr/bin/env bash

BUILD_DIR ?= tmp/build
NO_SMB_BUILD_DIR ?= tmp/build-no-smb
NATIVE_BUILD_DIR ?= tmp/build-native-no-legacy
PERF_BUILD_DIR ?= tmp/build-perf
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
SMOKE_TEST_REGEX ?= ui_smoke|main_window|connections_panel|connection_management_controller|credential_store_contract|qtkeychain_credential_store|encrypted_vault_credential_store|smb_client_contract|open_file_service|transfer_manager|remote_browser_widget|security_regression
WINDOWS_PACKAGE ?=
MACOS_PACKAGE ?=
POWERSHELL ?= pwsh

.DEFAULT_GOAL := help

.PHONY: help
help:
	@printf '%s\n' 'SMB Browser developer commands'
	@printf '\n%s\n' 'Common:'
	@printf '  %-22s %s\n' 'make setup' 'Install Ubuntu 22.04 build dependencies'
	@printf '  %-22s %s\n' 'make configure' 'Configure default build with native SMB backend'
	@printf '  %-22s %s\n' 'make build' 'Build default build directory'
	@printf '  %-22s %s\n' 'make test' 'Run default CTest suite'
	@printf '  %-22s %s\n' 'make run' 'Run the desktop app'
	@printf '  %-22s %s\n' 'make ci' 'Configure, build and test default profile'
	@printf '\n%s\n' 'Packaging:'
	@printf '  %-22s %s\n' 'make package-linux' 'Build Linux DEB package'
	@printf '  %-22s %s\n' 'make package-windows' 'Build Windows package via PowerShell helper'
	@printf '  %-22s %s\n' 'make package-macos' 'Build macOS app/DMG via macOS helper'
	@printf '  %-22s %s\n' 'make smoke-linux' 'Smoke-test generated Linux package'
	@printf '  %-22s %s\n' 'make smoke-windows' 'Smoke-test Windows ZIP package with PowerShell'
	@printf '  %-22s %s\n' 'make smoke-macos' 'Smoke-test macOS app/DMG package'
	@printf '  %-22s %s\n' 'make smoke-tests' 'Run cross-platform smoke CTest subset'
	@printf '  %-22s %s\n' 'make install' 'Install from BUILD_DIR using cmake --install'
	@printf '\n%s\n' 'Alternative profiles:'
	@printf '  %-22s %s\n' 'make no-smb' 'Configure/build/test without libsmb2 backend'
	@printf '  %-22s %s\n' 'make native-test' 'Run clean-room native SMB unit/protocol tests without libsmb2'
	@printf '  %-22s %s\n' 'make perf-test' 'Run opt-in native SMB perf/stress tests'
	@printf '  %-22s %s\n' 'make libsmb2' 'Build libsmb2 into tmp/libsmb2-prefix manually'
	@printf '  %-22s %s\n' 'make samba-up' 'Start Docker Samba fixture'
	@printf '  %-22s %s\n' 'make samba-test' 'Run opt-in Docker Samba integration tests'
	@printf '  %-22s %s\n' 'make samba-down' 'Stop Docker Samba fixture'
	@printf '\n%s\n' 'Maintenance:'
	@printf '  %-22s %s\n' 'make status' 'Show git status'
	@printf '  %-22s %s\n' 'make sbom' 'Generate release dependency manifest under tmp/sbom'
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
		libsodium-dev

.PHONY: configure
configure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_WITH_NATIVE_SMB=ON \
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
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_WITH_NATIVE_SMB=ON \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
	cmake --build $(PACKAGE_BUILD_DIR) --target package $(JOBS)

.PHONY: package-windows
package-windows:
	$(POWERSHELL) -File scripts/package-windows.ps1

.PHONY: package-macos
package-macos:
	scripts/package-macos.sh

.PHONY: smoke-linux
smoke-linux: package-linux
	scripts/package-smoke-linux.sh

.PHONY: smoke-windows
smoke-windows:
	$(POWERSHELL) -File scripts/package-smoke-windows.ps1 "$(WINDOWS_PACKAGE)"

.PHONY: smoke-macos
smoke-macos:
	scripts/package-smoke-macos.sh "$(MACOS_PACKAGE)"

.PHONY: smoke-tests
smoke-tests: build
	ctest --test-dir $(BUILD_DIR) -R '$(SMOKE_TEST_REGEX)' $(CTEST_ARGS)

.PHONY: install
install: build
	cmake --install $(BUILD_DIR)

.PHONY: no-smb-configure
no-smb-configure:
	cmake -S . -B $(NO_SMB_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_WITH_NATIVE_SMB=OFF \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

.PHONY: no-smb
no-smb: no-smb-configure
	cmake --build $(NO_SMB_BUILD_DIR) $(JOBS)
	ctest --test-dir $(NO_SMB_BUILD_DIR) $(CTEST_ARGS)

.PHONY: native-configure
native-configure:
	cmake -S . -B $(NATIVE_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_WITH_NATIVE_SMB=ON \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

.PHONY: native-test
native-test: native-configure
	cmake --build $(NATIVE_BUILD_DIR) \
		--target test_native_smb_scaffold test_native_smb_protocol test_native_smb_direct_tcp_transport test_native_smb_ntlm_messages test_native_smb_ntlm_crypto test_native_smb_spnego_token test_native_smb_signing test_native_smb_ntlm_v2_token_provider test_native_smb_connector test_native_smb_negotiator test_native_smb_session_setup test_native_smb_tree_connector test_native_smb_close_exchanger test_native_smb_change_notify_exchanger test_native_smb_read_exchanger test_native_smb_write_exchanger test_native_smb_set_info_exchanger test_native_smb_query_info_exchanger test_native_smb_directory_lister test_native_smb_directory_watcher test_native_smb_file_reader test_native_smb_file_writer test_native_smb_remote_object_operator test_native_smb_remote_stat_reader test_native_smb_remote_metadata_operator test_native_smb_session test_native_smb_error_mapper $(JOBS)
	ctest --test-dir $(NATIVE_BUILD_DIR) -L native-unit $(CTEST_ARGS)

.PHONY: perf-configure
perf-configure:
	cmake -S . -B $(PERF_BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSMB_BROWSER_WITH_LIBSMB2=OFF \
		-DSMB_BROWSER_WITH_NATIVE_SMB=ON \
		-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF \
		-DSMB_BROWSER_ENABLE_PERF_TESTS=ON

.PHONY: perf-test
perf-test: perf-configure
	cmake --build $(PERF_BUILD_DIR) --target test_native_smb_perf_stress $(JOBS)
	ctest --test-dir $(PERF_BUILD_DIR) -L perf $(CTEST_ARGS)

.PHONY: libsmb2
libsmb2:
	scripts/build-libsmb2.sh

.PHONY: samba-up
samba-up:
	$(DOCKER_COMPOSE) -f $(SAMBA_COMPOSE_FILE) up -d --build --wait --wait-timeout 60

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

.PHONY: sbom
sbom:
	scripts/generate-sbom.sh

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(NO_SMB_BUILD_DIR) $(NATIVE_BUILD_DIR) $(PERF_BUILD_DIR) $(PACKAGE_BUILD_DIR) $(SAMBA_BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf tmp
