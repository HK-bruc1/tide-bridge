# RDX configuration check and optional host mock Makefile.
# Run from tests/host/ directory.

ifeq ($(OS),Windows_NT)
    MAKE_PATH := $(subst \,/,$(CURDIR))/../../SDK/tools/utils/make.exe
    EXE_EXT   := .exe
    POWERSHELL := powershell.exe
    POWERSHELL_FLAGS := -NoProfile -ExecutionPolicy Bypass -File
    CONFIG_CC ?= C:/JL/pi32/bin/clang.exe
    RM        := $(subst \,/,$(CURDIR))/../../SDK/tools/utils/rm.exe
else
    EXE_EXT   :=
    RM        := rm
endif

# P9 must not add a native host compiler to the production prerequisites.
# Executable host mocks remain optional and require an explicit HOST_CC.
# The required configuration matrix is restricted to the existing Windows/JL
# production environment; it never falls back to tools from PATH.
HOST_CC ?=

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
                test_time_service \
                test_storage_port \
                test_rdx_p11_golden_trace

TEST_EXES    := $(addsuffix $(EXE_EXT),$(TESTS))

.PHONY: all host_all check_host_compiler clean rdx_config_matrix $(addsuffix .run,$(TESTS))
ifeq ($(OS),Windows_NT)
.PHONY: $(TESTS)
endif

all: rdx_config_matrix
	@echo Required RDX configuration checks passed.

host_all: check_host_compiler $(addsuffix .run,$(TESTS))
	@echo All host mock tests passed.

ifeq ($(OS),Windows_NT)
rdx_config_matrix:
	$(POWERSHELL) $(POWERSHELL_FLAGS) test_rdx_config_matrix.ps1 -Compiler "$(CONFIG_CC)" -MakeCommand "$(MAKE_PATH)"
else
rdx_config_matrix:
	@echo "ERROR: the required P9 configuration matrix must run in the existing Windows/JL production environment."
	@exit 1
endif

ifeq ($(strip $(HOST_CC)),)
check_host_compiler:
	@echo ERROR: host mocks are optional and require HOST_CC to be set explicitly.
ifeq ($(OS),Windows_NT)
	@exit /b 1
else
	@exit 1
endif
else
check_host_compiler:
	@echo Using explicitly supplied optional HOST_CC: $(HOST_CC)
endif

# Pattern rule: build any test_<name>.exe from test_<name>.c and its deps.
test_dispatch$(EXE_EXT): test_dispatch.c $(SERVICE_DIR)/rdx_command_dispatch.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_event_bus$(EXE_EXT): test_event_bus.c $(SERVICE_DIR)/rdx_event_bus.c mock/rdx_jl_osal_mock.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_time_ops$(EXE_EXT): test_time_ops.c mock/rdx_time_ops_host.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_board_config$(EXE_EXT): test_board_config.c $(BOARD_DIR)/rdx_board_config.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_util$(EXE_EXT): test_util.c $(PROTO_DIR)/rdx_util.c mock/rdx_debug_mock.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_time_service$(EXE_EXT): test_time_service.c $(SERVICE_DIR)/rdx_time_service.c $(SERVICE_DIR)/rdx_command_dispatch.c $(SERVICE_DIR)/rdx_event_bus.c mock/rdx_jl_osal_mock.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_storage_port$(EXE_EXT): test_storage_port.c $(PROTO_DIR)/port/jl/jl7018/rdx_jl_storage.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

test_rdx_p11_golden_trace$(EXE_EXT): test_rdx_p11_golden_trace.c rdx_p11_trace_spy.c | check_host_compiler
	$(HOST_CC) $(INCS) $^ -o $@

# Run a single test executable; this target depends on the .exe being up to date.
$(addsuffix .run,$(TESTS)): %.run: %$(EXE_EXT)
	@echo RUN: $*
	@./$*$(EXE_EXT)
	@echo PASS: $*

clean:
	$(RM) -f $(TEST_EXES)

# Convenience targets for individual tests
ifeq ($(OS),Windows_NT)
test_dispatch: test_dispatch.run
test_event_bus: test_event_bus.run
test_time_ops: test_time_ops.run
test_board_config: test_board_config.run
test_util: test_util.run
test_time_service: test_time_service.run
test_storage_port: test_storage_port.run
test_rdx_p11_golden_trace: test_rdx_p11_golden_trace.run
endif
