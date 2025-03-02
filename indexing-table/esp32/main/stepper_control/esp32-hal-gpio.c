// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp32-hal-gpio.h"
#include "hal/gpio_hal.h"
#include "soc/soc_caps.h"

// It fixes lack of pin definition for S3 and for any future SoC
// this function works for ESP32, ESP32-S2 and ESP32-S3 - including the C3, it will return -1 for any pin

#if SOC_TOUCH_SENSOR_NUM >  0
#include "soc/touch_sensor_periph.h"

int8_t digitalPinToTouchChannel(uint8_t pin)
{
    int8_t ret = -1;
    if (pin < SOC_GPIO_PIN_COUNT) {
        for (uint8_t i = 0; i < SOC_TOUCH_SENSOR_NUM; i++) {
            if (touch_sensor_channel_io_map[i] == pin) {
                ret = i;
                break;
            }
        }
    }
    return ret;
}
#endif

#ifndef CONFIG_IDF_TARGET_ESP32C6
int8_t digitalPinToAnalogChannel(uint8_t pin)
{
    uint8_t channel = 0;
    if (pin < SOC_GPIO_PIN_COUNT) {
        for (uint8_t i = 0; i < SOC_ADC_PERIPH_NUM; i++) {
            for (uint8_t j = 0; j < SOC_ADC_MAX_CHANNEL_NUM; j++) {
                if (adc_channel_io_map[i][j] == pin) {
                    return channel;
                }
                channel++;
            }
        }
    }
    return -1;
}
#endif

int8_t analogChannelToDigitalPin(uint8_t channel)
{
    if (channel >= (SOC_ADC_PERIPH_NUM * SOC_ADC_MAX_CHANNEL_NUM)) {
        return -1;
    }
    uint8_t adc_unit = (channel / SOC_ADC_MAX_CHANNEL_NUM);
    uint8_t adc_chan = (channel % SOC_ADC_MAX_CHANNEL_NUM);
    return adc_channel_io_map[adc_unit][adc_chan];
}

typedef void (*voidFuncPtr)(void);
typedef void (*voidFuncPtrArg)(void*);
typedef struct {
    voidFuncPtr fn;
    void* arg;
    bool functional;
} InterruptHandle_t;
static InterruptHandle_t pinInterruptHandlers[SOC_GPIO_PIN_COUNT] = {0,};

#include "driver/rtc_io.h"

extern void pinMode(uint8_t pin, uint8_t mode)
{

    if (!GPIO_IS_VALID_GPIO(pin)) {
        ESP_LOGE("GPIO-HAL", "Invalid pin selected");
        return;
    }

    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL<<pin),                 /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
        .mode = GPIO_MODE_DISABLE,                   /*!< GPIO mode: set input/output mode                     */
        .pull_up_en = GPIO_PULLUP_DISABLE,           /*!< GPIO pull-up                                         */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,       /*!< GPIO pull-down                                       */
        .intr_type = gpiohal.dev->pin[pin].int_type  /*!< GPIO interrupt type - previously set                 */
    };
    if (mode < 0x20) {//io
        conf.mode = mode & (INPUT | OUTPUT);
        if (mode & OPEN_DRAIN) {
            conf.mode |= GPIO_MODE_DEF_OD;
        }
        if (mode & PULLUP) {
            conf.pull_up_en = GPIO_PULLUP_ENABLE;
        }
        if (mode & PULLDOWN) {
            conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
    }
    if(gpio_config(&conf) != ESP_OK)
    {
        ESP_LOGE("GPIO-HAL", "GPIO config failed");
        return;
    }
}

extern void digitalWrite(uint8_t pin, uint8_t val)
{
	gpio_set_level((gpio_num_t)pin, val);
}

extern int digitalRead(uint8_t pin)
{
	return gpio_get_level((gpio_num_t)pin);
}

static void onPinInterrupt(void * arg) {
	InterruptHandle_t * isr = (InterruptHandle_t*)arg;
    if(isr->fn) {
        if(isr->arg){
            ((voidFuncPtrArg)isr->fn)(isr->arg);
        } else {
        	isr->fn();
        }
    }
}


extern void attachInterruptFunctionalArg(uint8_t pin, voidFuncPtrArg userFunc, void * arg, uint8_t interruptType, bool functional)
{
    static bool interrupt_initialized = false;

    if(!interrupt_initialized) {
    	esp_err_t err = gpio_install_isr_service(0);
    	interrupt_initialized = (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE);
    }
    if(!interrupt_initialized) {
    	ESP_LOGE("GPIO-HAL", "GPIO ISR Service Failed To Start");
    	return;
    }

    // if new attach without detach remove old info
    // if (pinInterruptHandlers[pin].functional && pinInterruptHandlers[pin].arg)
    // {
    // 	cleanupFunctional(pinInterruptHandlers[pin].arg);
    // }
    pinInterruptHandlers[pin].fn = (voidFuncPtr)userFunc;
    pinInterruptHandlers[pin].arg = arg;
    pinInterruptHandlers[pin].functional = functional;

    gpio_set_intr_type((gpio_num_t)pin, (gpio_int_type_t)(interruptType & 0x7));
    if(interruptType & 0x8){
    	gpio_wakeup_enable((gpio_num_t)pin, (gpio_int_type_t)(interruptType & 0x7));
    }
    gpio_isr_handler_add((gpio_num_t)pin, onPinInterrupt, &pinInterruptHandlers[pin]);


    //FIX interrupts on peripherals outputs (eg. LEDC,...)
    //Enable input in GPIO register
    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);
    gpio_hal_input_enable(&gpiohal, pin);
}

extern void attachInterruptArg(uint8_t pin, voidFuncPtrArg userFunc, void * arg, uint8_t interruptType)
{
	attachInterruptFunctionalArg(pin, userFunc, arg, interruptType, false);
}

extern void attachInterrupt(uint8_t pin, voidFuncPtr userFunc, uint8_t interruptType) {
    attachInterruptFunctionalArg(pin, (voidFuncPtrArg)userFunc, NULL, interruptType, false);
}

extern void detachInterrupt(uint8_t pin)
{
	gpio_isr_handler_remove((gpio_num_t)pin); //remove handle and disable isr for pin
	gpio_wakeup_disable((gpio_num_t)pin);

    // if (pinInterruptHandlers[pin].functional && pinInterruptHandlers[pin].arg)
    // {
    // 	detete( pinInterruptHandlers[pin].arg);
    // }
    pinInterruptHandlers[pin].fn = NULL;
    pinInterruptHandlers[pin].arg = NULL;
    pinInterruptHandlers[pin].functional = false;

    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
}
