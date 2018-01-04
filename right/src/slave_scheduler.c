#include "fsl_i2c.h"
#include "slave_scheduler.h"
#include "slot.h"
#include "slave_drivers/is31fl3731_driver.h"
#include "slave_drivers/uhk_module_driver.h"
#include "slave_drivers/kboot_driver.h"
#include "i2c.h"
#include "i2c_addresses.h"
#include "config.h"

uint32_t I2cSlaveScheduler_Counter;

static uint8_t previousSlaveId;
static uint8_t currentSlaveId;

uhk_slave_t Slaves[] = {
    {
        .init = UhkModuleSlaveDriver_Init,
        .update = UhkModuleSlaveDriver_Update,
        .disconnect = UhkModuleSlaveDriver_Disconnect,
        .perDriverId = UhkModuleDriverId_LeftKeyboardHalf,
    },
    {
        .init = UhkModuleSlaveDriver_Init,
        .update = UhkModuleSlaveDriver_Update,
        .perDriverId = UhkModuleDriverId_LeftAddon,
    },
    {
        .init = UhkModuleSlaveDriver_Init,
        .update = UhkModuleSlaveDriver_Update,
        .perDriverId = UhkModuleDriverId_RightAddon,
    },
#ifdef LED_DRIVERS_ENABLED
    {
        .init = LedSlaveDriver_Init,
        .update = LedSlaveDriver_Update,
        .perDriverId = LedDriverId_Right,
    },
    {
        .init = LedSlaveDriver_Init,
        .update = LedSlaveDriver_Update,
        .perDriverId = LedDriverId_Left,
    },
#endif
    {
        .init = KbootSlaveDriver_Init,
        .update = KbootSlaveDriver_Update,
        .perDriverId = KbootDriverId_Singleton,
    },
};

static void slaveSchedulerCallback(I2C_Type *base, i2c_master_handle_t *handle, status_t previousStatus, void *userData)
{
    bool isFirstIteration = true;
    bool isTransferScheduled = false;
    I2cSlaveScheduler_Counter++;

    do {
        uhk_slave_t *previousSlave = Slaves + previousSlaveId;
        uhk_slave_t *currentSlave = Slaves + currentSlaveId;

        previousSlave->previousStatus = previousStatus;

        if (isFirstIteration) {
            bool wasPreviousSlaveConnected = previousSlave->isConnected;
            previousSlave->isConnected = previousStatus == kStatus_Success;
            if (wasPreviousSlaveConnected && !previousSlave->isConnected && previousSlave->disconnect) {
                previousSlave->disconnect(previousSlaveId);
            }
            isFirstIteration = false;
        }

        if (!currentSlave->isConnected) {
            currentSlave->init(currentSlave->perDriverId);
        }

        status_t currentStatus = currentSlave->update(currentSlave->perDriverId);
        isTransferScheduled = currentStatus != kStatus_Uhk_IdleSlave && currentStatus != kStatus_Uhk_NoTransfer;
        if (isTransferScheduled) {
            currentSlave->isConnected = true;
        }

        if (currentStatus != kStatus_Uhk_NoTransfer) {
            previousSlaveId = currentSlaveId++;
        }

        if (currentSlaveId >= SLAVE_COUNT) {
            currentSlaveId = 0;
        }
    } while (!isTransferScheduled);
}

void InitSlaveScheduler(void)
{
    previousSlaveId = 0;
    currentSlaveId = 0;

    for (uint8_t i=0; i<SLAVE_COUNT; i++) {
        uhk_slave_t *currentSlave = Slaves + i;
        currentSlave->isConnected = false;
    }

    I2C_MasterTransferCreateHandle(I2C_MAIN_BUS_BASEADDR, &I2cMasterHandle, slaveSchedulerCallback, NULL);

    // Kickstart the scheduler by triggering the first transfer.
    slaveSchedulerCallback(I2C_MAIN_BUS_BASEADDR, &I2cMasterHandle, kStatus_Fail, NULL);
}
