#include "rdx_device_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"

/*
 * Phase 2 shell: currently forwards to existing rdx_app/rdx_vm functions.
 * Full extraction of factory_reset/unbind/shutdown orchestration from
 * rdx_vm.c happens after BLE/SPI callback contracts are verified.
 */

void rdx_device_service_init(void)
{
	RDX_LOGI("device_service init done");
}

int rdx_device_factory_reset(void)
{
	return RDX_ERR_NOTSUP;
}

int rdx_device_unpair(void)
{
	return RDX_ERR_NOTSUP;
}

int rdx_device_soft_poweroff(void)
{
	return RDX_ERR_NOTSUP;
}

int rdx_device_reboot(void)
{
	return RDX_ERR_NOTSUP;
}
