/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
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
#include "cmsis_os.h"

#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/**
 * @brief Estado do BMS
 */
typedef enum
{
  BMS_STATUS_NORMAL = 0,
  BMS_STATUS_WARNING,
  BMS_STATUS_CRITICAL
} BMS_Status_t;

/**
 * @brief Dados dos sensores
 */
typedef struct
{
  float voltage;     // tensão
  float current;     // corrente
  float temperature; // temperatura
  float soc;         // state of charge
} BMS_Data_t;

/**
 * @brief Estado completo do sistema
 */
typedef struct
{
  BMS_Data_t data;
  BMS_Status_t status;

  uint8_t overVoltage;
  uint8_t underVoltage;
  uint8_t overCurrent;
  uint8_t overTemperature;
} BMS_State_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* Definitions for MonitorTask */
osThreadId_t MonitorTaskHandle;
const osThreadAttr_t MonitorTask_attributes = {
    .name = "MonitorTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityLow,
};
/* Definitions for VoltageTask */
osThreadId_t VoltageTaskHandle;
const osThreadAttr_t VoltageTask_attributes = {
    .name = "VoltageTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};
/* Definitions for CurrentTask */
osThreadId_t CurrentTaskHandle;
const osThreadAttr_t CurrentTask_attributes = {
    .name = "CurrentTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};
/* Definitions for TemperatureTask */
osThreadId_t TemperatureTaskHandle;
const osThreadAttr_t TemperatureTask_attributes = {
    .name = "TemperatureTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};
/* Definitions for SafetyTask */
osThreadId_t SafetyTaskHandle;
const osThreadAttr_t SafetyTask_attributes = {
    .name = "SafetyTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityHigh,
};
/* Definitions for voltageQueue */
osMessageQueueId_t voltageQueueHandle;
const osMessageQueueAttr_t voltageQueue_attributes = {
    .name = "voltageQueue"};
/* Definitions for currentQueue */
osMessageQueueId_t currentQueueHandle;
const osMessageQueueAttr_t currentQueue_attributes = {
    .name = "currentQueue"};
/* Definitions for temperatureQueue */
osMessageQueueId_t temperatureQueueHandle;
const osMessageQueueAttr_t temperatureQueue_attributes = {
    .name = "temperatureQueue"};
/* Definitions for bmsStateQueue */
osMessageQueueId_t bmsStateQueueHandle;
const osMessageQueueAttr_t bmsStateQueue_attributes = {
    .name = "bmsStateQueue"};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void StartMonitorTask(void *argument);
void StartVoltageTask(void *argument);
void StartCurrentTask(void *argument);
void StartTemperatureTask(void *argument);
void StartSafetyTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Envia texto via UART2
 */
static void UART_Send(const char *message)
{
  HAL_UART_Transmit(
      &huart2,
      (uint8_t *)message,
      strlen(message),
      HAL_MAX_DELAY);
}

/**
 * @brief Converte status para string
 */
static const char *BMS_StatusToString(BMS_Status_t status)
{
  switch (status)
  {
  case BMS_STATUS_NORMAL:
    return "NORMAL";
  case BMS_STATUS_WARNING:
    return "WARNING";
  case BMS_STATUS_CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Avalia o estado de segurança do BMS
 */
static BMS_Status_t BMS_EvaluateSafety(BMS_State_t *state)
{
  state->overVoltage = 0;
  state->underVoltage = 0;
  state->overCurrent = 0;
  state->overTemperature = 0;

  /**
   * Limites de tensão
   */

  // Limite máximo de tensão
  if (state->data.voltage >= 54.6f)
  {
    state->overVoltage = 1;
  }

  // Limite mínimo de tensão
  if (state->data.voltage <= 40.0f)
  {
    state->underVoltage = 1;
  }

  /**
   * Limites de corrente
   */
  if (state->data.current >= 20.0f)
  {
    state->overCurrent = 1;
  }

  /**
   * Limites de temperatura
   */
  if (state->data.temperature >= 60.0f)
  {
    state->overTemperature = 1;
  }

  /**
   * Condições críticas
   */
  if (state->overVoltage || state->underVoltage || state->overCurrent || state->overTemperature)
  {
    return BMS_STATUS_CRITICAL;
  }

  /**
   * Condições de aviso
   */
  if (state->data.voltage >= 51.0f || state->data.voltage <= 42.0f ||
      state->data.current >= 15.0f || state->data.temperature >= 45.0f)
  {
    return BMS_STATUS_WARNING;
  }

  return BMS_STATUS_NORMAL;
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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of voltageQueue */
  voltageQueueHandle = osMessageQueueNew(4, sizeof(float), &voltageQueue_attributes);

  /* creation of currentQueue */
  currentQueueHandle = osMessageQueueNew(4, sizeof(float), &currentQueue_attributes);

  /* creation of temperatureQueue */
  temperatureQueueHandle = osMessageQueueNew(4, sizeof(float), &temperatureQueue_attributes);

  /* creation of bmsStateQueue */
  bmsStateQueueHandle = osMessageQueueNew(4, sizeof(BMS_State_t), &bmsStateQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */

  /* creation of MonitorTask */
  MonitorTaskHandle = osThreadNew(StartMonitorTask, NULL, &MonitorTask_attributes);

  /* creation of VoltageTask */
  VoltageTaskHandle = osThreadNew(StartVoltageTask, NULL, &VoltageTask_attributes);

  /* creation of CurrentTask */
  CurrentTaskHandle = osThreadNew(StartCurrentTask, NULL, &CurrentTask_attributes);

  /* creation of TemperatureTask */
  TemperatureTaskHandle = osThreadNew(StartTemperatureTask, NULL, &TemperatureTask_attributes);

  /* creation of SafetyTask */
  SafetyTaskHandle = osThreadNew(StartSafetyTask, NULL, &SafetyTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * Voltage task
 */
void StartVoltageTask(void *argument)
{
  (void)argument;

  float voltage = 48.0f;

  for (;;)
  {
    // Simula leitura de tensão
    voltage += 0.1f;
    if (voltage >= 50.f)
    {
      voltage = 48.0f;
    }

    osMessageQueuePut(
        voltageQueueHandle,
        &voltage,
        0,
        0);

    osDelay(500);
  }
}

/**
 * Current task
 */
void StartCurrentTask(void *argument)
{
  (void)argument;

  float current = 8.0f;

  for (;;)
  {
    // Simula leitura de corrente
    current += 0.5f;

    if (current >= 12.0f)
    {
      current = 8.0f;
    }

    osMessageQueuePut(
        currentQueueHandle,
        &current,
        0,
        0);

    osDelay(500);
  }
}

/**
 * Temperature task
 */
void StartTemperatureTask(void *argument)
{
  (void)argument;

  float temperature = 30.0f;

  for (;;)
  {
    // Simula leitura de temperatura
    temperature += 0.5f;

    if (temperature >= 35.0f)
    {
      temperature = 30.0f;
    }

    osMessageQueuePut(
        temperatureQueueHandle,
        &temperature,
        0,
        0);

    osDelay(500);
  }
}

/**
 * Safety task
 */
void StartSafetyTask(void *argument)
{
  (void)argument;

  BMS_State_t state;

  float voltage;
  float current;
  float temperature;

  // Valores inicias

  state.data.voltage = 48.0f;
  state.data.current = 8.0f;
  state.data.temperature = 30.0f;
  state.data.soc = 80.0f;

  state.status = BMS_STATUS_NORMAL;

  for (;;)
  {
    // Recebe dados de tensão
    if (osMessageQueueGet(
            voltageQueueHandle,
            &voltage,
            NULL,
            osWaitForever) == osOK)
    {
      state.data.voltage = voltage;
    }

    // Recebe dados de corrente
    if (osMessageQueueGet(
            currentQueueHandle,
            &current,
            NULL,
            osWaitForever) == osOK)
    {
      state.data.current = current;
    }

    // Recebe dados de temperatura
    if (osMessageQueueGet(
            temperatureQueueHandle,
            &temperature,
            NULL,
            osWaitForever) == osOK)
    {
      state.data.temperature = temperature;
    }

    // SOC simulado
    state.data.soc -= 0.01f;

    if (state.data.soc <= 0.0f)
    {
      state.data.soc = 100.0f;
    }

    // Avalia estado de segurança
    state.status = BMS_EvaluateSafety(&state);

    // Envia estado completo para a task de monitoramento
    osMessageQueuePut(
        bmsStateQueueHandle,
        &state,
        0,
        0);

    // Em condição crítica, apenas sinalizamos o problema. Futuramente: - contactor OFF, PWM OFF, shutdown, fault latch
    if (state.status == BMS_STATUS_CRITICAL)
    {
      HAL_GPIO_WritePin(
          LD2_GPIO_Port,
          LD2_Pin,
          GPIO_PIN_SET);
    }

    osDelay(100);
  }
}

/**
 * Monitor task
 */
void StartMonitorTask(void *argument)
{
  (void)argument;

  BMS_State_t state;

  char message[256];

  for (;;)
  {
    if (osMessageQueueGet(
            bmsStateQueueHandle,
            &state,
            NULL,
            osWaitForever) == osOK)
    {
      /* ------------------------------------------------- */
      /* Header                                             */
      /* ------------------------------------------------- */

      UART_Send("\r\n");
      UART_Send("================================\r\n");
      UART_Send("           VoltOS BMS\r\n");
      UART_Send("================================\r\n");

      /* ------------------------------------------------- */
      /* Tensão                                            */
      /* ------------------------------------------------- */

      snprintf(
          message,
          sizeof(message),
          "[BMS] Voltage: %.2f V\r\n",
          state.data.voltage);

      UART_Send(message);

      /* ------------------------------------------------- */
      /* Corrente                                          */
      /* ------------------------------------------------- */

      snprintf(
          message,
          sizeof(message),
          "[BMS] Current: %.2f A\r\n",
          state.data.current);

      UART_Send(message);

      /* ------------------------------------------------- */
      /* Temperatura                                       */
      /* ------------------------------------------------- */

      snprintf(
          message,
          sizeof(message),
          "[BMS] Temperature: %.2f C\r\n",
          state.data.temperature);

      UART_Send(message);

      /* ------------------------------------------------- */
      /* SOC                                                */
      /* ------------------------------------------------- */

      snprintf(
          message,
          sizeof(message),
          "[BMS] SOC: %.2f %%\r\n",
          state.data.soc);

      UART_Send(message);

      /* ------------------------------------------------- */
      /* Status                                             */
      /* ------------------------------------------------- */

      snprintf(
          message,
          sizeof(message),
          "[BMS] Status: %s\r\n",
          BMS_StatusToString(state.status));

      UART_Send(message);

      /* ------------------------------------------------- */
      /* Faults                                             */
      /* ------------------------------------------------- */

      if (state.overVoltage)
      {
        UART_Send("[BMS] !!! OVERVOLTAGE !!!\r\n");
      }

      if (state.underVoltage)
      {
        UART_Send("[BMS] !!! UNDERVOLTAGE !!!\r\n");
      }

      if (state.overCurrent)
      {
        UART_Send("[BMS] !!! OVERCURRENT !!!\r\n");
      }

      if (state.overTemperature)
      {
        UART_Send("[BMS] !!! OVER TEMPERATURE !!!\r\n");
      }

      /* ------------------------------------------------- */
      /* Shutdown indication                               */
      /* ------------------------------------------------- */

      if (state.status == BMS_STATUS_CRITICAL)
      {
        UART_Send(
            "[BMS] !!! SHUTDOWN REQUEST !!!\r\n");
      }

      UART_Send("================================\r\n");
    }

    osDelay(100);
  }
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

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
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

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
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
