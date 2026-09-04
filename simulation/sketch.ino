#include <Arduino.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdint.h>
#include <stdio.h>

/* ============================================================
 * VoltOS - Simplified BMS
 * Wokwi / STM32 Nucleo-C031C6
 * ============================================================ */


/* ============================================================
 * BMS STATUS
 * ============================================================ */

typedef enum
{
    BMS_STATUS_NORMAL = 0,
    BMS_STATUS_WARNING,
    BMS_STATUS_CRITICAL

} BMS_Status_t;


/* ============================================================
 * BMS DATA
 * ============================================================ */

typedef struct
{
    float voltage;
    float current;
    float temperature;
    float soc;

} BMS_Data_t;


/* ============================================================
 * BMS STATE
 * ============================================================ */

typedef struct
{
    BMS_Data_t data;

    BMS_Status_t status;

    bool overVoltage;
    bool underVoltage;
    bool overCurrent;
    bool overTemperature;

} BMS_State_t;


/* ============================================================
 * FAULT TYPE
 * ============================================================ */

typedef enum
{
    FAULT_NONE = 0,
    FAULT_OVERVOLTAGE,
    FAULT_UNDERVOLTAGE,
    FAULT_OVERCURRENT,
    FAULT_OVERTEMPERATURE

} FaultType_t;


/* ============================================================
 * LED PINS
 * ============================================================ */

/*
 * LED onboard do Nucleo
 * PA5 / D13
 *
 * Representa o estado CRITICAL geral.
 */
#define LED_NORMAL LED_BUILTIN


/*
 * LEDs externos
 *
 * OV = Overvoltage
 * UV = Undervoltage
 * OC = Overcurrent
 * OT = Overtemperature
 */

#define LED_OVERVOLTAGE     PB0
#define LED_UNDERVOLTAGE    PB1
#define LED_OVERCURRENT     PB2
#define LED_OVERTEMPERATURE PB3


/* ============================================================
 * PROTECTION LIMITS
 * ============================================================ */

#define MAX_VOLTAGE     54.6f
#define MIN_VOLTAGE     40.0f
#define MAX_CURRENT     20.0f
#define MAX_TEMPERATURE 60.0f


/* ============================================================
 * WARNING LIMITS
 * ============================================================ */

#define WARNING_HIGH_VOLTAGE 51.0f
#define WARNING_LOW_VOLTAGE  42.0f
#define WARNING_CURRENT      15.0f
#define WARNING_TEMPERATURE  45.0f


/* ============================================================
 * FAULT INJECTION
 * ============================================================ */

#define NORMAL_STATE_TIME_MS 10000
#define FAULT_STATE_TIME_MS   5000


#define FAULT_OVERVOLTAGE_VALUE   55.0f
#define FAULT_UNDERVOLTAGE_VALUE  39.0f
#define FAULT_OVERCURRENT_VALUE   25.0f
#define FAULT_OVERTEMP_VALUE      65.0f


/* ============================================================
 * QUEUES
 * ============================================================ */

QueueHandle_t voltageQueue;
QueueHandle_t currentQueue;
QueueHandle_t temperatureQueue;
QueueHandle_t bmsStateQueue;


/* ============================================================
 * GLOBAL STATE
 * ============================================================ */

volatile FaultType_t activeFault = FAULT_NONE;

volatile bool faultInjectionActive = false;

volatile bool systemCritical = false;


/* ============================================================
 * TASK HANDLES
 * ============================================================ */

TaskHandle_t monitorTaskHandle;
TaskHandle_t voltageTaskHandle;
TaskHandle_t currentTaskHandle;
TaskHandle_t temperatureTaskHandle;
TaskHandle_t safetyTaskHandle;
TaskHandle_t faultInjectionTaskHandle;


/* ============================================================
 * HELPER - STATUS NAME
 * ============================================================ */

const char* getStatusName(BMS_Status_t status)
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


/* ============================================================
 * HELPER - FAULT NAME
 * ============================================================ */

const char* getFaultName(FaultType_t fault)
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
            return "OVERTEMPERATURE";

        default:
            return "NONE";
    }
}


/* ============================================================
 * VOLTAGE TASK
 * ============================================================ */

void VoltageTask(void *pvParameters)
{
    float voltage = 48.0f;

    while (1)
    {
        if (faultInjectionActive)
        {
            if (activeFault == FAULT_OVERVOLTAGE)
            {
                voltage = FAULT_OVERVOLTAGE_VALUE;
            }
            else if (activeFault == FAULT_UNDERVOLTAGE)
            {
                voltage = FAULT_UNDERVOLTAGE_VALUE;
            }
        }
        else
        {
            voltage += 0.1f;

            if (voltage >= 50.0f)
            {
                voltage = 48.0f;
            }
        }

        xQueueSend(
            voltageQueue,
            &voltage,
            10
        );

        vTaskDelay(
            pdMS_TO_TICKS(500)
        );
    }
}


/* ============================================================
 * CURRENT TASK
 * ============================================================ */

void CurrentTask(void *pvParameters)
{
    float current = 8.0f;

    while (1)
    {
        if (faultInjectionActive &&
            activeFault == FAULT_OVERCURRENT)
        {
            current = FAULT_OVERCURRENT_VALUE;
        }
        else
        {
            current += 0.5f;

            if (current >= 12.0f)
            {
                current = 8.0f;
            }
        }

        xQueueSend(
            currentQueue,
            &current,
            10
        );

        vTaskDelay(
            pdMS_TO_TICKS(500)
        );
    }
}


/* ============================================================
 * TEMPERATURE TASK
 * ============================================================ */

void TemperatureTask(void *pvParameters)
{
    float temperature = 30.0f;

    while (1)
    {
        if (faultInjectionActive &&
            activeFault == FAULT_OVERTEMPERATURE)
        {
            temperature = FAULT_OVERTEMP_VALUE;
        }
        else
        {
            temperature += 0.5f;

            if (temperature >= 35.0f)
            {
                temperature = 30.0f;
            }
        }

        xQueueSend(
            temperatureQueue,
            &temperature,
            10
        );

        vTaskDelay(
            pdMS_TO_TICKS(500)
        );
    }
}


/* ============================================================
 * SAFETY TASK
 * ============================================================ */

void SafetyTask(void *pvParameters)
{
    BMS_State_t state;

    float voltage = 48.0f;
    float current = 8.0f;
    float temperature = 30.0f;
    float soc = 100.0f;

    float receivedVoltage;
    float receivedCurrent;
    float receivedTemperature;

    while (1)
    {
        /* ----------------------------------------------------
         * Read voltage
         * ---------------------------------------------------- */

        if (xQueueReceive(
                voltageQueue,
                &receivedVoltage,
                0
            ) == pdPASS)
        {
            voltage = receivedVoltage;
        }


        /* ----------------------------------------------------
         * Read current
         * ---------------------------------------------------- */

        if (xQueueReceive(
                currentQueue,
                &receivedCurrent,
                0
            ) == pdPASS)
        {
            current = receivedCurrent;
        }


        /* ----------------------------------------------------
         * Read temperature
         * ---------------------------------------------------- */

        if (xQueueReceive(
                temperatureQueue,
                &receivedTemperature,
                0
            ) == pdPASS)
        {
            temperature = receivedTemperature;
        }


        /* ----------------------------------------------------
         * SOC simulation
         * ---------------------------------------------------- */

        soc -= 0.01f;

        if (soc <= 0.0f)
        {
            soc = 100.0f;
        }


        /* ----------------------------------------------------
         * Build BMS state
         * ---------------------------------------------------- */

        state.data.voltage = voltage;
        state.data.current = current;
        state.data.temperature = temperature;
        state.data.soc = soc;


        /* ----------------------------------------------------
         * Safety flags
         * ---------------------------------------------------- */

        state.overVoltage =
            voltage > MAX_VOLTAGE;

        state.underVoltage =
            voltage < MIN_VOLTAGE;

        state.overCurrent =
            current > MAX_CURRENT;

        state.overTemperature =
            temperature > MAX_TEMPERATURE;


        /* ----------------------------------------------------
         * Individual fault LEDs
         *
         * Each LED represents one protection fault.
         * ---------------------------------------------------- */

        digitalWrite(
            LED_OVERVOLTAGE,
            state.overVoltage ? HIGH : LOW
        );

        digitalWrite(
            LED_UNDERVOLTAGE,
            state.underVoltage ? HIGH : LOW
        );

        digitalWrite(
            LED_OVERCURRENT,
            state.overCurrent ? HIGH : LOW
        );

        digitalWrite(
            LED_OVERTEMPERATURE,
            state.overTemperature ? HIGH : LOW
        );


        /* ----------------------------------------------------
         * Determine BMS status
         * ---------------------------------------------------- */

        if (state.overVoltage ||
            state.underVoltage ||
            state.overCurrent ||
            state.overTemperature)
        {
            state.status = BMS_STATUS_CRITICAL;

            systemCritical = true;

            /*
             * Onboard LED:
             * CRITICAL = ON
             */
            digitalWrite(
                LED_NORMAL,
                HIGH
            );
        }
        else if (
            voltage > WARNING_HIGH_VOLTAGE ||
            voltage < WARNING_LOW_VOLTAGE ||
            current > WARNING_CURRENT ||
            temperature > WARNING_TEMPERATURE)
        {
            state.status = BMS_STATUS_WARNING;

            systemCritical = false;

            digitalWrite(
                LED_NORMAL,
                LOW
            );
        }
        else
        {
            state.status = BMS_STATUS_NORMAL;

            systemCritical = false;

            digitalWrite(
                LED_NORMAL,
                LOW
            );
        }


        /* ----------------------------------------------------
         * Send BMS state to MonitorTask
         * ---------------------------------------------------- */

        xQueueSend(
            bmsStateQueue,
            &state,
            10
        );


        vTaskDelay(
            pdMS_TO_TICKS(100)
        );
    }
}


/* ============================================================
 * MONITOR TASK
 * ============================================================ */

void MonitorTask(void *pvParameters)
{
    BMS_State_t state;

    while (1)
    {
        if (xQueueReceive(
                bmsStateQueue,
                &state,
                portMAX_DELAY
            ) == pdPASS)
        {
            Serial.println();
            Serial.println("----------------------------------------");

            Serial.print("Voltage:     ");
            Serial.print(
                state.data.voltage,
                2
            );
            Serial.println(" V");

            Serial.print("Current:     ");
            Serial.print(
                state.data.current,
                2
            );
            Serial.println(" A");

            Serial.print("Temperature: ");
            Serial.print(
                state.data.temperature,
                2
            );
            Serial.println(" C");

            Serial.print("SOC:         ");
            Serial.print(
                state.data.soc,
                2
            );
            Serial.println(" %");

            Serial.print("Status:      ");
            Serial.println(
                getStatusName(state.status)
            );

            Serial.print("OV: ");
            Serial.print(
                state.overVoltage ? "YES" : "NO"
            );

            Serial.print(" | UV: ");
            Serial.print(
                state.underVoltage ? "YES" : "NO"
            );

            Serial.print(" | OC: ");
            Serial.print(
                state.overCurrent ? "YES" : "NO"
            );

            Serial.print(" | OT: ");
            Serial.println(
                state.overTemperature ? "YES" : "NO"
            );


            if (systemCritical)
            {
                Serial.print("ACTIVE FAULT: ");
                Serial.println(
                    getFaultName(activeFault)
                );

                Serial.println(
                    "*** CRITICAL FAULT ***"
                );
            }


            Serial.println(
                "LED STATUS:"
            );

            Serial.print("  OV: ");
            Serial.println(
                state.overVoltage ? "ON" : "OFF"
            );

            Serial.print("  UV: ");
            Serial.println(
                state.underVoltage ? "ON" : "OFF"
            );

            Serial.print("  OC: ");
            Serial.println(
                state.overCurrent ? "ON" : "OFF"
            );

            Serial.print("  OT: ");
            Serial.println(
                state.overTemperature ? "ON" : "OFF"
            );

            Serial.println("----------------------------------------");
        }
    }
}


/* ============================================================
 * FAULT INJECTION TASK
 * ============================================================ */

void FaultInjectionTask(void *pvParameters)
{
    /*
     * Automatic fault sequence:
     *
     * NORMAL
     *      ↓
     * OVERVOLTAGE
     *      ↓
     * NORMAL
     *      ↓
     * UNDERVOLTAGE
     *      ↓
     * NORMAL
     *      ↓
     * OVERCURRENT
     *      ↓
     * NORMAL
     *      ↓
     * OVERTEMPERATURE
     *      ↓
     * NORMAL
     *      ↓
     * repeat
     */

    FaultType_t sequence[] =
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


    const int sequenceSize =
        sizeof(sequence) / sizeof(sequence[0]);


    int index = 0;


    while (1)
    {
        activeFault = sequence[index];


        /* ----------------------------------------------------
         * NORMAL OPERATION
         * ---------------------------------------------------- */

        if (activeFault == FAULT_NONE)
        {
            faultInjectionActive = false;

            Serial.println();
            Serial.println(
                "[FAULT] System operating normally"
            );

            vTaskDelay(
                pdMS_TO_TICKS(
                    NORMAL_STATE_TIME_MS
                )
            );
        }


        /* ----------------------------------------------------
         * FAULT ACTIVE
         * ---------------------------------------------------- */

        else
        {
            faultInjectionActive = true;


            switch (activeFault)
            {
                case FAULT_OVERVOLTAGE:

                    Serial.println();
                    Serial.println(
                        "[FAULT] OVERVOLTAGE injected!"
                    );

                    break;


                case FAULT_UNDERVOLTAGE:

                    Serial.println();
                    Serial.println(
                        "[FAULT] UNDERVOLTAGE injected!"
                    );

                    break;


                case FAULT_OVERCURRENT:

                    Serial.println();
                    Serial.println(
                        "[FAULT] OVERCURRENT injected!"
                    );

                    break;


                case FAULT_OVERTEMPERATURE:

                    Serial.println();
                    Serial.println(
                        "[FAULT] OVERTEMPERATURE injected!"
                    );

                    break;


                default:
                    break;
            }


            /*
             * Keep fault active for 5 seconds.
             */

            vTaskDelay(
                pdMS_TO_TICKS(
                    FAULT_STATE_TIME_MS
                )
            );


            /* ------------------------------------------------
             * Remove fault
             * ------------------------------------------------ */

            activeFault = FAULT_NONE;

            faultInjectionActive = false;


            Serial.println();
            Serial.println(
                "[FAULT] Fault removed - recovery"
            );


            /*
             * Recovery period.
             */

            vTaskDelay(
                pdMS_TO_TICKS(
                    NORMAL_STATE_TIME_MS
                )
            );
        }


        /* ----------------------------------------------------
         * Next fault
         * ---------------------------------------------------- */

        index++;

        if (index >= sequenceSize)
        {
            index = 0;
        }
    }
}


/* ============================================================
 * SETUP
 * ============================================================ */

void setup()
{
    /* --------------------------------------------------------
     * Serial
     * -------------------------------------------------------- */

    Serial.begin(115200);


    /* --------------------------------------------------------
     * LED configuration
     * -------------------------------------------------------- */

    pinMode(
        LED_NORMAL,
        OUTPUT
    );

    pinMode(
        LED_OVERVOLTAGE,
        OUTPUT
    );

    pinMode(
        LED_UNDERVOLTAGE,
        OUTPUT
    );

    pinMode(
        LED_OVERCURRENT,
        OUTPUT
    );

    pinMode(
        LED_OVERTEMPERATURE,
        OUTPUT
    );


    /* --------------------------------------------------------
     * Start with all LEDs OFF
     * -------------------------------------------------------- */

    digitalWrite(
        LED_NORMAL,
        LOW
    );

    digitalWrite(
        LED_OVERVOLTAGE,
        LOW
    );

    digitalWrite(
        LED_UNDERVOLTAGE,
        LOW
    );

    digitalWrite(
        LED_OVERCURRENT,
        LOW
    );

    digitalWrite(
        LED_OVERTEMPERATURE,
        LOW
    );


    /* --------------------------------------------------------
     * Banner
     * -------------------------------------------------------- */

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "           VoltOS BMS"
    );

    Serial.println(
        "        FreeRTOS Simulation"
    );

    Serial.println(
        "        STM32 Nucleo-C031C6"
    );

    Serial.println(
        "========================================"
    );

    Serial.println();


    /* --------------------------------------------------------
     * Create queues
     * -------------------------------------------------------- */

    voltageQueue = xQueueCreate(
        4,
        sizeof(float)
    );

    currentQueue = xQueueCreate(
        4,
        sizeof(float)
    );

    temperatureQueue = xQueueCreate(
        4,
        sizeof(float)
    );

    bmsStateQueue = xQueueCreate(
        2,
        sizeof(BMS_State_t)
    );


    /* --------------------------------------------------------
     * Validate queues
     * -------------------------------------------------------- */

    if (voltageQueue == NULL ||
        currentQueue == NULL ||
        temperatureQueue == NULL ||
        bmsStateQueue == NULL)
    {
        Serial.println(
            "ERROR: Failed to create queues!"
        );


        while (1)
        {
            digitalWrite(
                LED_NORMAL,
                HIGH
            );

            delay(200);

            digitalWrite(
                LED_NORMAL,
                LOW
            );

            delay(200);
        }
    }


    /* --------------------------------------------------------
     * Create tasks
     * -------------------------------------------------------- */

    xTaskCreate(
        MonitorTask,
        "Monitor",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        1,
        &monitorTaskHandle
    );


    xTaskCreate(
        VoltageTask,
        "Voltage",
        configMINIMAL_STACK_SIZE,
        NULL,
        2,
        &voltageTaskHandle
    );


    xTaskCreate(
        CurrentTask,
        "Current",
        configMINIMAL_STACK_SIZE,
        NULL,
        2,
        &currentTaskHandle
    );


    xTaskCreate(
        TemperatureTask,
        "Temperature",
        configMINIMAL_STACK_SIZE,
        NULL,
        2,
        &temperatureTaskHandle
    );


    xTaskCreate(
        SafetyTask,
        "Safety",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        4,
        &safetyTaskHandle
    );


    xTaskCreate(
        FaultInjectionTask,
        "FaultInjection",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        3,
        &faultInjectionTaskHandle
    );


    Serial.println(
        "Starting FreeRTOS scheduler..."
    );


    /* --------------------------------------------------------
     * Start scheduler
     * -------------------------------------------------------- */

    vTaskStartScheduler();


    /*
     * Should never reach here.
     */

    Serial.println(
        "ERROR: Scheduler stopped!"
    );


    while (1)
    {
        digitalWrite(
            LED_NORMAL,
            HIGH
        );

        delay(100);

        digitalWrite(
            LED_NORMAL,
            LOW
        );

        delay(100);
    }
}


/* ============================================================
 * LOOP
 * ============================================================ */

void loop()
{
    /*
     * FreeRTOS controls the application.
     */
}