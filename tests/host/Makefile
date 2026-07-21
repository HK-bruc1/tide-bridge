# RDX Host Mock Test Makefile
# Run from tests/host/ directory.
# Uses repository-local make.exe on Windows if available, otherwise system make.

# Detect make: prefer repo-local, fallback to PATH.
ifeq ($(OS),Windows_NT)
    MAKE_PATH := $(subst \,/,$(CURDIR))/../../SDK/tools/utils/make.exe
    EXE_EXT   := .exe
    POWERSHELL := powershell.exe
    POWERSHELL_FLAGS := -NoProfile -ExecutionPolicy Bypass -File
else
    MAKE_PATH := make
    EXE_EXT   :=
    POWERSHELL ?= pwsh
    POWERSHELL_FLAGS := -NoProfile -File
endif

MAKE         := $(MAKE_PATH)
RM           := $(subst \,/,$(CURDIR))/../../SDK/tools/utils/rm.exe

# Host C compiler detection. The configuration validator requires C11
# _Static_assert support, so TinyCC 0.9.27 is not a valid fallback.
# Override with: make CC=path/to/gcc
ifeq ($(origin CC),default)
    CC := gcc
endif

# Source roots
PROTO_DIR    := ../../SDK/apps/common/third_party_profile/rdx_protocol
SERVICE_DIR  := $(PROTO_DIR)/service
PORT_DIR     := $(PROTO_DIR)/port/jl/include
BOARD_DIR    := $(PROTO_DIR)/board/t2616_cc
BOARD_COMMON_DIR := $(PROTO_DIR)/board/common

# Mock headers shadow JL-specific includes (typedef.h, system/includes.h, etc.)
MOCK_INC     := -Imock/include

# Include paths for real RDX code
INCS         := $(MOCK_INC) \
                -I$(PROTO_DIR) \
                -I$(SERVICE_DIR) \
                -I$(PORT_DIR) \
                -I$(BOARD_COMMON_DIR) \
                -I$(BOARD_DIR)

# Tests: each maps to an executable target
TESTS        := test_dispatch \
                test_event_bus \
                test_time_ops \
                test_board_config \
                test_util \
                test_time_service

TEST_EXES    := $(addsuffix $(EXE_EXT),$(TESTS))

.PHONY: all clean rdx_config_matrix $(TESTS) $(addsuffix .run,$(TESTS))

all: rdx_config_matrix $(addsuffix .run,$(TESTS))
	@echo All host mock tests passed.

rdx_config_matrix:
	$(POWERSHELL) $(POWERSHELL_FLAGS) test_rdx_config_matrix.ps1 -Compiler "$(CC)" -MakeCommand "$(MAKE_PATH)"

# Pattern rule: build any test_<name>.exe from test_<name>.c and its deps.
test_dispatch$(EXE_EXT): test_dispatch.c $(SERVICE_DIR)/rdx_command_dispatch.c
	$(CC) $(INCS) $^ -o $@

test_event_bus$(EXE_EXT): test_event_bus.c $(SERVICE_DIR)/rdx_event_bus.c mock/rdx_jl_osal_mock.c
	$(CC) $(INCS) $^ -o $@

test_time_ops$(EXE_EXT): test_time_ops.c mock/rdx_time_ops_host.c
	$(CC) $(INCS) $^ -o $@

test_board_config$(EXE_EXT): test_board_config.c $(BOARD_DIR)/rdx_board_config.c
	$(CC) $(INCS) $^ -o $@

test_util$(EXE_EXT): test_util.c $(PROTO_DIR)/rdx_util.c mock/rdx_debug_mock.c
	$(CC) $(INCS) $^ -o $@

test_time_service$(EXE_EXT): test_time_service.c $(SERVICE_DIR)/rdx_time_service.c $(SERVICE_DIR)/rdx_command_dispatch.c $(SERVICE_DIR)/rdx_event_bus.c mock/rdx_jl_osal_mock.c
	$(CC) $(INCS) $^ -o $@

# Run a single test executable; this target depends on the .exe being up to date.
$(addsuffix .run,$(TESTS)): %.run: %$(EXE_EXT)
	@echo RUN: $*
	@./$*$(EXE_EXT)
	@echo PASS: $*

clean:
	$(RM) -f $(TEST_EXES)

# Convenience targets for individual tests
test_dispatch: test_dispatch.run
test_event_bus: test_event_bus.run
test_time_ops: test_time_ops.run
test_board_config: test_board_config.run
test_util: test_util.run
test_time_service: test_time_service.run
