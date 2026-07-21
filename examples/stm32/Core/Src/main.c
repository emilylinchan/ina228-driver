/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ina228.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RX_BUF_SIZE    64

#define INA228_ADDR         0x40    // 7-bit I2C address (0x40..0x4F)
#define INA228_SHUNT_OHMS   0.015f  // Shunt resistor value (Ohms)
#define INA228_MAX_AMPS     1.0f    // Max expected current (A)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static ina228_t ina;

static char rxBuf[RX_BUF_SIZE];
static int  rxIndex;

static int total_time = 0;   // seconds
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */
ina228_t ina228_stm32_make(uint8_t addr_7bit, float shunt_ohms, float max_amps);

static void UART_ReceiveLine(void);
static int  Parse_Command(void);
static void Stream_Data(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Reads incoming data from the PC one byte at a time until it
  *         receives a newline character '\n'.
  * @retval None
  */
static void UART_ReceiveLine(void) 
{
  rxIndex = 0;
  memset(rxBuf, 0, sizeof(rxBuf));

  while (1) {
    uint8_t byte;
    HAL_UART_Receive(&huart2, &byte, 1, HAL_MAX_DELAY);

    if (byte == '\n')
      break;

    if (rxIndex < RX_BUF_SIZE - 1)
      rxBuf[rxIndex++] = (char)byte;
  }
}

/**
  * @brief  Checks if the received line is a valid START command and
  *         extracts the requested duration into total_time.
  * @retval 1 if a valid START command was parsed, 0 otherwise
  */
static int Parse_Command(void) 
{
  if (strncmp(rxBuf, "START", 5) != 0)
    return 0;

  char *tok = strtok(rxBuf, ",");   // "START"
  tok = strtok(NULL, ",");          // total_time
  if (!tok) return 0;
  total_time = atoi(tok);

  if (total_time <= 0)
    return 0;

  return 1;
}

/**
  * @brief  Streams timestamped samples to the PC as they are acquired, for
  *         total_time seconds. Each line is:
  *         <ms_since_start>,<voltage>,<current>,<power>.
  *         Sampling runs as fast as the INA228 produces conversions.
  * @retval None
  */
static void Stream_Data(void) 
{
  uint8_t healthy = 0;
  ina228_is_healthy(&ina, &healthy);

  if (!healthy) {
    const char fault[] = "FAULT_SENSOR_COMM\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)fault, sizeof(fault)-1, HAL_MAX_DELAY);
    return;
  }

  char line[64];
  uint32_t t0 = HAL_GetTick(); // start time (ms)

  while ((HAL_GetTick() - t0) < (uint32_t)total_time * 1000u) {
    // Wait for a fresh measurement
    uint8_t ready = 0;
    if (ina228_conversion_ready(&ina, &ready) != INA228_OK) {
      const char fault[] = "FAULT_SENSOR_COMM\n";
      HAL_UART_Transmit(&huart2, (uint8_t*)fault, sizeof(fault)-1, HAL_MAX_DELAY);
      return;
    }
    if (!ready)
      continue;

    // Read and store sensor measurements
    float v = 0.0f, c = 0.0f, p = 0.0f;
    ina228_read_voltage(&ina, &v);
    ina228_read_current(&ina, &c);
    ina228_read_power  (&ina, &p);

    uint32_t ts = HAL_GetTick() - t0;   // time since start (ms)

    // Format sensor data for UART Tx
    int len = snprintf(line, sizeof(line),
                       "%lu,%.6f,%.6f,%.6f\n",
                       (unsigned long)ts, v, c, p);
    HAL_UART_Transmit(&huart2, (uint8_t*)line, len, HAL_MAX_DELAY);
  }

  const char done[] = "DONE\n";
  HAL_UART_Transmit(&huart2, (uint8_t*)done, sizeof(done)-1, HAL_MAX_DELAY);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  ina = ina228_stm32_make(INA228_ADDR, INA228_SHUNT_OHMS, INA228_MAX_AMPS);
  if (ina228_init(&ina) != INA228_OK) {
    const char *msg = "INA228 INIT FAILED\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    UART_ReceiveLine();

    if (Parse_Command()) {
      const char ok[] = "OK\n";
      HAL_UART_Transmit(&huart2, (uint8_t*)ok, sizeof(ok)-1, HAL_MAX_DELAY);
      Stream_Data();
    }
    else {
      const char err[] = "ERR\n";
      HAL_UART_Transmit(&huart2, (uint8_t*)err, sizeof(err)-1, HAL_MAX_DELAY);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}