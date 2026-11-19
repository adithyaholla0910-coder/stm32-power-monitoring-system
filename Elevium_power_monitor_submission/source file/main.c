#include "main.h"
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef hadc1;

/* LCD Pin Definitions (4-bit mode on GPIOB) */
#define RS_PORT GPIOB
#define RS_PIN  GPIO_PIN_0
#define EN_PORT GPIOB
#define EN_PIN  GPIO_PIN_1

#define D4_PORT GPIOB
#define D5_PORT GPIOB
#define D6_PORT GPIOB
#define D7_PORT GPIOB
#define D4_PIN  GPIO_PIN_12
#define D5_PIN  GPIO_PIN_13
#define D6_PIN  GPIO_PIN_14
#define D7_PIN  GPIO_PIN_15

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);

/* 16x2 LCD Driver Functions */
void lcd_send_nibble(uint8_t nibble) {
    HAL_GPIO_WritePin(D4_PORT, D4_PIN, (nibble & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(D5_PORT, D5_PIN, (nibble & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(D6_PORT, D6_PIN, (nibble & 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(D7_PORT, D7_PIN, (nibble & 8) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(EN_PORT, EN_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(EN_PORT, EN_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
}

void lcd_send_cmd(uint8_t cmd) {
    HAL_GPIO_WritePin(RS_PORT, RS_PIN, GPIO_PIN_RESET);
    lcd_send_nibble(cmd >> 4);
    lcd_send_nibble(cmd & 0x0F);
}

void lcd_send_data(uint8_t data) {
    HAL_GPIO_WritePin(RS_PORT, RS_PIN, GPIO_PIN_SET);
    lcd_send_nibble(data >> 4);
    lcd_send_nibble(data & 0x0F);
}

void lcd_init(void) {
    HAL_Delay(50);
    HAL_GPIO_WritePin(RS_PORT, RS_PIN, GPIO_PIN_RESET);
    lcd_send_nibble(0x03);
    HAL_Delay(5);
    lcd_send_nibble(0x03);
    HAL_Delay(1);
    lcd_send_nibble(0x03);
    HAL_Delay(1);
    lcd_send_nibble(0x02);
    HAL_Delay(1);

    lcd_send_cmd(0x28);
    lcd_send_cmd(0x0C);
    lcd_send_cmd(0x06);
    lcd_send_cmd(0x01);
    HAL_Delay(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col) {
    uint8_t address = (row == 0) ? 0x80 : 0xC0;
    lcd_send_cmd(address | col);
}

void lcd_print(char *str) {
    while (*str) {
        lcd_send_data(*str++);
    }
}

/* Single-Channel ADC Read Helper */
uint32_t read_adc_channel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();

    lcd_init();

    while (1) {
        /* 1. Read Voltage (PA0) and Current (PA1) */
        uint32_t adc_v_raw = read_adc_channel(ADC_CHANNEL_0);
        uint32_t adc_i_raw = read_adc_channel(ADC_CHANNEL_1);

        /* 2. Convert Raw ADC Counts to Real Units (3.3V Reference) */
        float v_adc = (adc_v_raw * 3.3f) / 4095.0f;
        float voltage = v_adc * ((15.0f + 3.3f) / 3.3f);

        float i_adc = (adc_i_raw * 3.3f) / 4095.0f;
        float current = (i_adc - 2.5f) / 0.185f;
        if (current < 0.0f) current = -current;
        if (current < 0.05f) current = 0.0f;

        float power = voltage * current;

        /* 3. Fault Detection (V > 13.0V or I > 1.0A) */
        char fault_status[10] = "NORMAL";
        uint8_t is_fault = 0;

        if (voltage > 13.0f && current > 1.0f) {
            strcpy(fault_status, "V&I FLT");
            is_fault = 1;
        } else if (voltage > 13.0f) {
            strcpy(fault_status, "OV-VOLT");
            is_fault = 1;
        } else if (current > 1.0f) {
            strcpy(fault_status, "OV-CURR");
            is_fault = 1;
        }

        /* 4. Trigger LED (PA8) and Buzzer (PA9) */
        if (is_fault) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
        }

        /* 5. Update LCD Smoothly */
        char line1[20];
        char line2[20];
        sprintf(line1, "V:%05.2fV I:%04.2fA", voltage, current);
        sprintf(line2, "P:%5.1fW %-7s", power, fault_status);

        lcd_set_cursor(0, 0);
        lcd_print(line1);
        lcd_set_cursor(1, 0);
        lcd_print(line2);

        HAL_Delay(500);
    }
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV2;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

static void MX_ADC1_Init(void) {
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_12 |
                             GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_12 |
                          GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}
