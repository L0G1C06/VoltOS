/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : VoltOS - Simplified BMS with Fault Injection
  ******************************************************************************
  * @attention
  *
  * VoltOS BMS
  *
  * Simulação de um Battery Management System utilizando:
  *
  *  - STM32F401RE
  *  - FreeRTOS / CMSIS-RTOS2
  *  - UART2
  *  - Message Queues
  *  - Tasks independentes para sensores
  *  - Safety Task
  *  - Fault Injection
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
    float voltage;
    float current;
    float temperature;
    float soc;

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


/**
 * @brief Tipos de falha que podem ser injetados.
 *
 * A falha não substitui a simulação.
 * Ela apenas modifica temporariamente o valor
 * produzido pelo sensor.
 */
typedef enum
{
    FAULT_NONE = 0,

    FAULT_OVERVOLTAGE,
    FAULT_UNDERVOLTAGE,
    FAULT_OVERCURRENT,
    FAULT_OVERTEMPERATURE

} FaultType_t;


/* USER CODE END PTD */


/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * ============================================================
 * BMS PROTECTION LIMITS
 * ============================================================
 */

/* Voltage */
#define BMS_MAX_VOLTAGE          54.6f
#define BMS_MIN_VOLTAGE          40.0f

/* Current */
#define BMS_MAX_CURRENT          20.0f

/* Temperature */
#define BMS_MAX_TEMPERATURE      60.0f


/*
 * ============================================================
 * WARNING LIMITS
 * ============================================================
 */

#define BMS_WARNING_HIGH_VOLTAGE    51.0f
#define BMS_WARNING_LOW_VOLTAGE     42.0f

#define BMS_WARNING_CURRENT         15.0f

#define BMS_WARNING_TEMPERATURE     45.0f


/*
 * ============================================================
 * FAULT INJECTION
 * ============================================================
 *
 * Cada estado permanece durante alguns segundos.
 *
 * NORMAL
 *   ↓
 * OVERVOLTAGE
 *   ↓
 * NORMAL
 *   ↓
 * UNDERVOLTAGE
 *   ↓
 * NORMAL
 *   ↓
 * OVERCURRENT
 *   ↓
 * NORMAL
 *   ↓
 * OVERTEMPERATURE
 *   ↓
 * NORMAL
 *   ↓
 * repete
 *
 */


/*
 * Tempo em estado normal antes da primeira falha.
 */
#define FAULT_NORMAL_TIME_MS       10000U


/*
 * Duração de cada falha.
 */
#define FAULT_DURATION_MS           5000U


/*
 * Tempo entre mudanças de estado.
 */
#define FAULT_CYCLE_TIME_MS        15000U


/*
 * Valores utilizados somente durante a injeção.
 *
 * Esses valores ultrapassam deliberadamente
 * os limites de proteção.
 */

#define INJECTED_OVERVOLTAGE        55.0f
#define INJECTED_UNDERVOLTAGE       39.0f
#define INJECTED_OVERCURRENT        25.0f
#define INJECTED_OVERTEMPERATURE    65.0f


/* USER CODE END PD */


/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */


/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart2;


/* Definitions for MonitorTask ----------------------------------------------*/

osThreadId_t MonitorTaskHandle;

const osThreadAttr_t MonitorTask_attributes =
{
    .name = "MonitorTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityLow,
};


/* Definitions for VoltageTask ----------------------------------------------*/

osThreadId_t VoltageTaskHandle;

const osThreadAttr_t VoltageTask_attributes =
{
    .name = "VoltageTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};


/* Definitions for CurrentTask ----------------------------------------------*/

osThreadId_t CurrentTaskHandle;

const osThreadAttr_t CurrentTask_attributes =
{
    .name = "CurrentTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};


/* Definitions for TemperatureTask ------------------------------------------*/

osThreadId_t TemperatureTaskHandle;

const osThreadAttr_t TemperatureTask_attributes =
{
    .name = "TemperatureTask",
    .stack_size = 256 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};


/* Definitions for SafetyTask -----------------------------------------------*/

osThreadId_t SafetyTaskHandle;

const osThreadAttr_t SafetyTask_attributes =
{
    .name = "SafetyTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityHigh,
};


/* Definitions for FaultInjectionTask ---------------------------------------*/

osThreadId_t FaultInjectionTaskHandle;

const osThreadAttr_t FaultInjectionTask_attributes =
{
    .name = "FaultInjectionTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityAboveNormal,
};


/* Definitions for voltageQueue ---------------------------------------------*/

osMessageQueueId_t voltageQueueHandle;

const osMessageQueueAttr_t voltageQueue_attributes =
{
    .name = "voltageQueue"
};


/* Definitions for currentQueue ---------------------------------------------*/

osMessageQueueId_t currentQueueHandle;

const osMessageQueueAttr_t currentQueue_attributes =
{
    .name = "currentQueue"
};


/* Definitions for temperatureQueue -----------------------------------------*/

osMessageQueueId_t temperatureQueueHandle;

const osMessageQueueAttr_t temperatureQueue_attributes =
{
    .name = "temperatureQueue"
};


/* Definitions for bmsStateQueue --------------------------------------------*/

osMessageQueueId_t bmsStateQueueHandle;

const osMessageQueueAttr_t bmsStateQueue_attributes =
{
    .name = "bmsStateQueue"
};


/* USER CODE BEGIN PV */


/**
 * @brief Falha atualmente injetada.
 *
 * volatile porque é acessada por várias Tasks.
 */
static volatile FaultType_t activeFault = FAULT_NONE;


/**
 * @brief Indica se existe uma falha ativa.
 */
static volatile uint8_t faultInjectionActive = 0;


/**
 * @brief Estado crítico detectado pelo SafetyTask.
 *
 * Diferente de um shutdown permanente:
 * o sistema continua executando para que possamos
 * observar a recuperação após a remoção da falha.
 */
static volatile uint8_t systemCritical = 0;


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

void StartFaultInjectionTask(void *argument);


/* USER CODE BEGIN PFP */

static void UART_Send(const char *message);

static const char *BMS_StatusToString(BMS_Status_t status);

static const char *FaultTypeToString(FaultType_t fault);

static BMS_Status_t BMS_EvaluateSafety(BMS_State_t *state);


/* USER CODE END PFP */


/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/**
 * @brief Envia texto via UART2.
 */
static void UART_Send(const char *message)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)message,
        strlen(message),
        HAL_MAX_DELAY
    );
}


/**
 * @brief Converte status para string.
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
 * @brief Converte tipo de falha para texto.
 */
static const char *FaultTypeToString(FaultType_t fault)
{
    switch (fault)
    {
        case FAULT_OVERVOLTAGE:
            return "OVERVOLTAGE";

        case FAULT_UNDERVOLTAGE:
            return "UNDERVOLTAGE";

        case FAULT_OVERCURRENT:
            return "OVERCURRENT";

        case FAULT_OVERTEMPERATURE:
            return "OVER TEMPERATURE";

        case FAULT_NONE:
        default:
            return "NONE";
    }
}


/**
 * @brief Avalia o estado de segurança do BMS.
 */
static BMS_Status_t BMS_EvaluateSafety(BMS_State_t *state)
{
    state->overVoltage = 0;
    state->underVoltage = 0;
    state->overCurrent = 0;
    state->overTemperature = 0;


    /*
     * ========================================================
     * VOLTAGE
     * ========================================================
     */

    if (state->data.voltage >= BMS_MAX_VOLTAGE)
    {
        state->overVoltage = 1;
    }

    if (state->data.voltage <= BMS_MIN_VOLTAGE)
    {
        state->underVoltage = 1;
    }


    /*
     * ========================================================
     * CURRENT
     * ========================================================
     */

    if (state->data.current >= BMS_MAX_CURRENT)
    {
        state->overCurrent = 1;
    }


    /*
     * ========================================================
     * TEMPERATURE
     * ========================================================
     */

    if (state->data.temperature >= BMS_MAX_TEMPERATURE)
    {
        state->overTemperature = 1;
    }


    /*
     * ========================================================
     * CRITICAL
     * ========================================================
     */

    if (
        state->overVoltage ||
        state->underVoltage ||
        state->overCurrent ||
        state->overTemperature
       )
    {
        return BMS_STATUS_CRITICAL;
    }


    /*
     * ========================================================
     * WARNING
     * ========================================================
     */

    if (
        state->data.voltage >= BMS_WARNING_HIGH_VOLTAGE ||
        state->data.voltage <= BMS_WARNING_LOW_VOLTAGE ||
        state->data.current >= BMS_WARNING_CURRENT ||
        state->data.temperature >= BMS_WARNING_TEMPERATURE
       )
    {
        return BMS_STATUS_WARNING;
    }


    return BMS_STATUS_NORMAL;
}


/* USER CODE END 0 */


/**
 * @brief  The application entry point.
 */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */


    /* MCU Configuration --------------------------------------*/

    HAL_Init();


    /* Configure system clock */
    SystemClock_Config();


    /* Initialize peripherals */
    MX_GPIO_Init();

    MX_USART2_UART_Init();


    /* USER CODE BEGIN 2 */

    UART_Send("\r\n");
    UART_Send("================================\r\n");
    UART_Send("           VoltOS BMS\r\n");
    UART_Send("================================\r\n");

    UART_Send("[SYSTEM] Initializing...\r\n");

    UART_Send("[SYSTEM] Sensor simulation: ENABLED\r\n");

    UART_Send("[SYSTEM] Fault injection: ENABLED\r\n");

    UART_Send("[SYSTEM] Automatic fault cycle: ENABLED\r\n");

    UART_Send("================================\r\n");


    /* USER CODE END 2 */


    /* Init scheduler */
    osKernelInitialize();


    /* Create queues ------------------------------------------*/

    voltageQueueHandle =
        osMessageQueueNew(
            4,
            sizeof(float),
            &voltageQueue_attributes
        );


    currentQueueHandle =
        osMessageQueueNew(
            4,
            sizeof(float),
            &currentQueue_attributes
        );


    temperatureQueueHandle =
        osMessageQueueNew(
            4,
            sizeof(float),
            &temperatureQueue_attributes
        );


    bmsStateQueueHandle =
        osMessageQueueNew(
            4,
            sizeof(BMS_State_t),
            &bmsStateQueue_attributes
        );


    /* Create tasks -------------------------------------------*/


    MonitorTaskHandle =
        osThreadNew(
            StartMonitorTask,
            NULL,
            &MonitorTask_attributes
        );


    VoltageTaskHandle =
        osThreadNew(
            StartVoltageTask,
            NULL,
            &VoltageTask_attributes
        );


    CurrentTaskHandle =
        osThreadNew(
            StartCurrentTask,
            NULL,
            &CurrentTask_attributes
        );


    TemperatureTaskHandle =
        osThreadNew(
            StartTemperatureTask,
            NULL,
            &TemperatureTask_attributes
        );


    SafetyTaskHandle =
        osThreadNew(
            StartSafetyTask,
            NULL,
            &SafetyTask_attributes
        );


    FaultInjectionTaskHandle =
        osThreadNew(
            StartFaultInjectionTask,
            NULL,
            &FaultInjectionTask_attributes
        );


    /* Start scheduler */

    osKernelStart();


    while (1)
    {
    }
}


/**
 * @brief Voltage Task
 *
 * Simula o comportamento normal da tensão.
 *
 * IMPORTANTE:
 * A falha é aplicada DEPOIS da simulação normal.
 */
void StartVoltageTask(void *argument)
{
    (void)argument;


    float voltage = 48.0f;

    float simulatedVoltage;


    for (;;)
    {
        /*
         * ====================================================
         * SIMULAÇÃO REAL DO SENSOR
         * ====================================================
         */

        voltage += 0.1f;


        if (voltage >= 50.0f)
        {
            voltage = 48.0f;
        }


        /*
         * Começamos com o valor físico simulado.
         */
        simulatedVoltage = voltage;


        /*
         * ====================================================
         * FAULT INJECTION
         * ====================================================
         *
         * A simulação continua existindo.
         * Apenas sobrescrevemos temporariamente
         * o valor enviado ao BMS.
         */

        if (activeFault == FAULT_OVERVOLTAGE)
        {
            simulatedVoltage = INJECTED_OVERVOLTAGE;
        }


        if (activeFault == FAULT_UNDERVOLTAGE)
        {
            simulatedVoltage = INJECTED_UNDERVOLTAGE;
        }


        /*
         * Envia valor para SafetyTask.
         */

        osMessageQueuePut(
            voltageQueueHandle,
            &simulatedVoltage,
            0,
            0
        );


        osDelay(500);
    }
}


/**
 * @brief Current Task
 *
 * Simulação normal da corrente.
 */
void StartCurrentTask(void *argument)
{
    (void)argument;


    float current = 8.0f;

    float simulatedCurrent;


    for (;;)
    {
        /*
         * ====================================================
         * SIMULAÇÃO NORMAL
         * ====================================================
         */

        current += 0.5f;


        if (current >= 12.0f)
        {
            current = 8.0f;
        }


        simulatedCurrent = current;


        /*
         * ====================================================
         * FAULT INJECTION
         * ====================================================
         */

        if (activeFault == FAULT_OVERCURRENT)
        {
            simulatedCurrent = INJECTED_OVERCURRENT;
        }


        /*
         * Envia corrente.
         */

        osMessageQueuePut(
            currentQueueHandle,
            &simulatedCurrent,
            0,
            0
        );


        osDelay(500);
    }
}


/**
 * @brief Temperature Task
 *
 * Simulação normal da temperatura.
 */
void StartTemperatureTask(void *argument)
{
    (void)argument;


    float temperature = 30.0f;

    float simulatedTemperature;


    for (;;)
    {
        /*
         * ====================================================
         * SIMULAÇÃO NORMAL
         * ====================================================
         */

        temperature += 0.5f;


        if (temperature >= 35.0f)
        {
            temperature = 30.0f;
        }


        simulatedTemperature = temperature;


        /*
         * ====================================================
         * FAULT INJECTION
         * ====================================================
         */

        if (activeFault == FAULT_OVERTEMPERATURE)
        {
            simulatedTemperature = INJECTED_OVERTEMPERATURE;
        }


        /*
         * Envia temperatura.
         */

        osMessageQueuePut(
            temperatureQueueHandle,
            &simulatedTemperature,
            0,
            0
        );


        osDelay(500);
    }
}


/**
 * @brief Fault Injection Task
 *
 * Controla o ciclo automático de falhas.
 *
 * IMPORTANTE:
 *
 * Esta task NÃO simula diretamente os sensores.
 *
 * Ela apenas informa qual falha deve ser aplicada.
 *
 * Os sensores continuam executando sua simulação
 * normalmente.
 */
void StartFaultInjectionTask(void *argument)
{
    (void)argument;


    FaultType_t faults[] =
    {
        FAULT_NONE,
        FAULT_OVERVOLTAGE,
        FAULT_NONE,
        FAULT_UNDERVOLTAGE,
        FAULT_NONE,
        FAULT_OVERCURRENT,
        FAULT_NONE,
        FAULT_OVERTEMPERATURE,
        FAULT_NONE
    };


    const uint32_t faultCount =
        sizeof(faults) / sizeof(faults[0]);


    uint32_t index = 0;


    char message[128];


    /*
     * ========================================================
     * INITIAL STATE
     * ========================================================
     */

    activeFault = FAULT_NONE;

    faultInjectionActive = 0;


    for (;;)
    {
        /*
         * Define o próximo estado.
         */

        activeFault = faults[index];


        /*
         * ====================================================
         * NORMAL
         * ====================================================
         */

        if (activeFault == FAULT_NONE)
        {
            faultInjectionActive = 0;


            UART_Send(
                "\r\n"
                "[FAULT] --------------------------------\r\n"
                "[FAULT] Injection state: NORMAL\r\n"
                "[FAULT] Sensor simulation running normally\r\n"
                "[FAULT] --------------------------------\r\n"
            );


            osDelay(FAULT_NORMAL_TIME_MS);
        }


        /*
         * ====================================================
         * FAULT
         * ====================================================
         */

        else
        {
            faultInjectionActive = 1;


            snprintf(
                message,
                sizeof(message),
                "\r\n"
                "[FAULT] ################################\r\n"
                "[FAULT] FAULT INJECTION ACTIVE\r\n"
                "[FAULT] Type: %s\r\n"
                "[FAULT] ################################\r\n",
                FaultTypeToString(activeFault)
            );


            UART_Send(message);


            /*
             * Mantém a falha durante o período definido.
             */

            osDelay(FAULT_DURATION_MS);


            /*
             * Remove a falha.
             */

            activeFault = FAULT_NONE;

            faultInjectionActive = 0;


            UART_Send(
                "\r\n"
                "[FAULT] Fault injection removed\r\n"
                "[FAULT] Returning to real sensor simulation\r\n"
            );


            /*
             * Dá tempo para o BMS observar
             * a recuperação.
             */

            osDelay(FAULT_NORMAL_TIME_MS);
        }


        /*
         * Próxima falha.
         */

        index++;


        if (index >= faultCount)
        {
            index = 0;
        }
    }
}


/**
 * @brief Safety Task
 *
 * Recebe os sensores e avalia o estado do BMS.
 */
void StartSafetyTask(void *argument)
{
    (void)argument;


    BMS_State_t state;


    float voltage;
    float current;
    float temperature;


    state.data.voltage = 48.0f;
    state.data.current = 8.0f;
    state.data.temperature = 30.0f;
    state.data.soc = 80.0f;

    state.status = BMS_STATUS_NORMAL;

    state.overVoltage = 0;
    state.underVoltage = 0;
    state.overCurrent = 0;
    state.overTemperature = 0;


    for (;;)
    {
        /*
         * ====================================================
         * VOLTAGE
         * ====================================================
         */

        if (
            osMessageQueueGet(
                voltageQueueHandle,
                &voltage,
                NULL,
                osWaitForever
            ) == osOK
           )
        {
            state.data.voltage = voltage;
        }


        /*
         * ====================================================
         * CURRENT
         * ====================================================
         */

        if (
            osMessageQueueGet(
                currentQueueHandle,
                &current,
                NULL,
                osWaitForever
            ) == osOK
           )
        {
            state.data.current = current;
        }


        /*
         * ====================================================
         * TEMPERATURE
         * ====================================================
         */

        if (
            osMessageQueueGet(
                temperatureQueueHandle,
                &temperature,
                NULL,
                osWaitForever
            ) == osOK
           )
        {
            state.data.temperature = temperature;
        }


        /*
         * ====================================================
         * SOC
         * ====================================================
         *
         * Simulação simples de descarga.
         */

        state.data.soc -= 0.01f;


        if (state.data.soc <= 0.0f)
        {
            state.data.soc = 100.0f;
        }


        /*
         * ====================================================
         * SAFETY EVALUATION
         * ====================================================
         */

        state.status =
            BMS_EvaluateSafety(&state);


        /*
         * ====================================================
         * CRITICAL STATE
         * ====================================================
         */

        if (state.status == BMS_STATUS_CRITICAL)
        {
            systemCritical = 1;

            HAL_GPIO_WritePin(
                LD2_GPIO_Port,
                LD2_Pin,
                GPIO_PIN_SET
            );
        }
        else
        {
            systemCritical = 0;

            /*
             * Quando a falha desaparece,
             * o sistema volta ao estado normal.
             */

            HAL_GPIO_WritePin(
                LD2_GPIO_Port,
                LD2_Pin,
                GPIO_PIN_RESET
            );
        }


        /*
         * Envia estado completo para MonitorTask.
         */

        osMessageQueuePut(
            bmsStateQueueHandle,
            &state,
            0,
            0
        );


        osDelay(100);
    }
}


/**
 * @brief Monitor Task
 *
 * Mostra o estado completo do BMS via UART.
 */
void StartMonitorTask(void *argument)
{
    (void)argument;


    BMS_State_t state;


    char message[256];


    for (;;)
    {
        if (
            osMessageQueueGet(
                bmsStateQueueHandle,
                &state,
                NULL,
                osWaitForever
            ) == osOK
           )
        {
            /*
             * =================================================
             * HEADER
             * =================================================
             */

            UART_Send("\r\n");

            UART_Send("================================\r\n");

            UART_Send("           VoltOS BMS\r\n");

            UART_Send("================================\r\n");


            /*
             * =================================================
             * VOLTAGE
             * =================================================
             */

            snprintf(
                message,
                sizeof(message),
                "[BMS] Voltage: %.2f V\r\n",
                state.data.voltage
            );

            UART_Send(message);


            /*
             * =================================================
             * CURRENT
             * =================================================
             */

            snprintf(
                message,
                sizeof(message),
                "[BMS] Current: %.2f A\r\n",
                state.data.current
            );

            UART_Send(message);


            /*
             * =================================================
             * TEMPERATURE
             * =================================================
             */

            snprintf(
                message,
                sizeof(message),
                "[BMS] Temperature: %.2f C\r\n",
                state.data.temperature
            );

            UART_Send(message);


            /*
             * =================================================
             * SOC
             * =================================================
             */

            snprintf(
                message,
                sizeof(message),
                "[BMS] SOC: %.2f %%\r\n",
                state.data.soc
            );

            UART_Send(message);


            /*
             * =================================================
             * STATUS
             * =================================================
             */

            snprintf(
                message,
                sizeof(message),
                "[BMS] Status: %s\r\n",
                BMS_StatusToString(state.status)
            );

            UART_Send(message);


            /*
             * =================================================
             * FAULT INJECTION STATUS
             * =================================================
             */

            if (faultInjectionActive)
            {
                snprintf(
                    message,
                    sizeof(message),
                    "[BMS] Injected Fault: %s\r\n",
                    FaultTypeToString(activeFault)
                );

                UART_Send(message);
            }
            else
            {
                UART_Send(
                    "[BMS] Injected Fault: NONE\r\n"
                );
            }


            /*
             * =================================================
             * PROTECTION FLAGS
             * =================================================
             */

            if (state.overVoltage)
            {
                UART_Send(
                    "[BMS] !!! OVERVOLTAGE !!!\r\n"
                );
            }


            if (state.underVoltage)
            {
                UART_Send(
                    "[BMS] !!! UNDERVOLTAGE !!!\r\n"
                );
            }


            if (state.overCurrent)
            {
                UART_Send(
                    "[BMS] !!! OVERCURRENT !!!\r\n"
                );
            }


            if (state.overTemperature)
            {
                UART_Send(
                    "[BMS] !!! OVER TEMPERATURE !!!\r\n"
                );
            }


            /*
             * =================================================
             * CRITICAL
             * =================================================
             */

            if (state.status == BMS_STATUS_CRITICAL)
            {
                UART_Send(
                    "[BMS] !!! CRITICAL CONDITION !!!\r\n"
                );

                UART_Send(
                    "[BMS] !!! SAFETY RESPONSE ACTIVE !!!\r\n"
                );
            }


            UART_Send(
                "================================\r\n"
            );
        }


        osDelay(100);
    }
}


/**
 * @brief System Clock Configuration
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};

    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};


    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_PWR_VOLTAGESCALING_CONFIG(
        PWR_REGULATOR_VOLTAGE_SCALE2
    );


    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSI;

    RCC_OscInitStruct.HSIState =
        RCC_HSI_ON;

    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSI;

    RCC_OscInitStruct.PLL.PLLM = 16;

    RCC_OscInitStruct.PLL.PLLN = 336;

    RCC_OscInitStruct.PLL.PLLP =
        RCC_PLLP_DIV4;

    RCC_OscInitStruct.PLL.PLLQ = 7;


    if (
        HAL_RCC_OscConfig(
            &RCC_OscInitStruct
        ) != HAL_OK
       )
    {
        Error_Handler();
    }


    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV2;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;


    if (
        HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_2
        ) != HAL_OK
       )
    {
        Error_Handler();
    }
}


/**
 * @brief USART2 Initialization Function
 */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;

    huart2.Init.BaudRate = 115200;

    huart2.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart2.Init.StopBits =
        UART_STOPBITS_1;

    huart2.Init.Parity =
        UART_PARITY_NONE;

    huart2.Init.Mode =
        UART_MODE_TX_RX;

    huart2.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart2.Init.OverSampling =
        UART_OVERSAMPLING_16;


    if (
        HAL_UART_Init(&huart2)
        != HAL_OK
       )
    {
        Error_Handler();
    }
}


/**
 * @brief GPIO Initialization Function
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};


    __HAL_RCC_GPIOC_CLK_ENABLE();

    __HAL_RCC_GPIOH_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();


    HAL_GPIO_WritePin(
        LD2_GPIO_Port,
        LD2_Pin,
        GPIO_PIN_RESET
    );


    /*
     * PC13
     */

    GPIO_InitStruct.Pin =
        GPIO_PIN_13;

    GPIO_InitStruct.Mode =
        GPIO_MODE_IT_RISING;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        GPIOC,
        &GPIO_InitStruct
    );


    /*
     * LD2
     */

    GPIO_InitStruct.Pin =
        LD2_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_OUTPUT_PP;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_LOW;


    HAL_GPIO_Init(
        LD2_GPIO_Port,
        &GPIO_InitStruct
    );
}


/**
 * @brief Error Handler
 */
void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}


#ifdef USE_FULL_ASSERT

void assert_failed(
    uint8_t *file,
    uint32_t line
)
{
    (void)file;
    (void)line;
}

#endif