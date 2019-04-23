/*
 * sensor.c
 *
 *  Created on: Apr 21, 2019
 *      Author: nachi
 */

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/sysctl.h"
#include "driverlib/debug.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "inc/hw_memmap.h"

#include "sensor.h"
#include "spi.h"
#include "driverlib/gpio.h"
#include "driverlib/adc.h"
#include "driverlib/pin_map.h"
#include "driverlib/ssi.h"

// FreeRTOS includes
#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

extern uint32_t g_ui32SysClock;
extern TaskHandle_t TempTaskHandle;
extern TaskHandle_t SMTaskHandle;
extern QueueHandle_t IBQueue;

void moisture_sensor_init()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCIntClear(ADC0_BASE, 3);
}

uint32_t moisture_data()
{
    uint32_t data;
    ADCProcessorTrigger(ADC0_BASE, 3);
    while(!ADCIntStatus(ADC0_BASE, 3, false))
    {
    }
    ADCIntClear(ADC0_BASE, 3);
    ADCSequenceDataGet(ADC0_BASE, 3, &data);
    SysCtlDelay(g_ui32SysClock / 12);
    return data;
}


void temp_sens_init(uint32_t mode, uint32_t clk_speed)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI3);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));

    GPIOPinConfigure(GPIO_PF0_SSI3XDAT1);
    GPIOPinConfigure(GPIO_PF1_SSI3XDAT0);
    GPIOPinConfigure(GPIO_PF2_SSI3FSS);
    GPIOPinConfigure(GPIO_PF3_SSI3CLK);

    GPIOPinTypeSSI(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    SSIConfigSetExpClk(SSI3_BASE, g_ui32SysClock, SSI_FRF_MOTO_MODE_0, mode, clk_speed, 16);
    SSIEnable(SSI3_BASE);
}


uint16_t temp_data_get()
{
    uint32_t buffer;
    uint16_t data_to_write = 0x1234;
    SSIDataPut(SSI3_BASE, data_to_write);
    SSIDataGet(SSI3_BASE, &buffer);
    return (uint16_t)buffer;
}


void TemperatureTask(void *pvParameters)
{
    IBStruct data_to_send;
    data_to_send.source = 0x03;
//  Initialize the temperature sensor
    temp_sens_init(MASTER, TEMP_SPI_CLK);
//  Initialize the timer for periodic measurements
    TimerHandle_t TakeTempReadings = xTimerCreate("TakeTemperature", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, TemperatureCallback);
//  Start the timer after 100ms
    BaseType_t return_val = xTimerStart(TakeTempReadings, pdMS_TO_TICKS(0));
    while(1)
    {
        // Wait for notification from the timer to take reading from sensors
        xTaskNotifyWait(0xffffffff,0xffffffff,NULL,portMAX_DELAY);

        // Take the reading from the sensor
        data_to_send.data = temp_data_get();

        // Send it to the queue of the SPI task
        xQueueSend(IBQueue, &data_to_send, pdMS_TO_TICKS(0));
    }
}


void TemperatureCallback(TimerHandle_t xtimer)
{
    // Notify the task to take the readings
    xTaskNotify(TempTaskHandle, 1, eNoAction);
}


void SoilMoistureTask(void *pvParameters)
{
    IBStruct data_to_send;
    data_to_send.source = 0x30;
    // Initialize the soil moisture sensor ADC.
    moisture_sensor_init();
    // Initialize the timer for periodic measurements
    TimerHandle_t TakeSoilReadings = xTimerCreate("TakeSoilMoisture", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, MoistureCallback);
    //  Start the timer after 100ms
    BaseType_t return_val = xTimerStart(TakeSoilReadings, pdMS_TO_TICKS(500));
    while(1)
    {
        // Wait for notification from the timer to take reading from sensors
        xTaskNotifyWait(0xffffffff,0xffffffff,NULL,portMAX_DELAY);

        // Take the reading from the sensor
        data_to_send.data = moisture_data();

        // Send it to the queue of the SPI task
        xQueueSend(IBQueue, &data_to_send, pdMS_TO_TICKS(0));
    }
}


void MoistureCallback(TimerHandle_t xtimer)
{
    // Notify the task to take the readings
    xTaskNotify(SMTaskHandle, 1, eNoAction);
}


float temperature_in_c(uint16_t hex_val)
{
    hex_val = hex_val >> 3;
    return (hex_val*0.25);
}
