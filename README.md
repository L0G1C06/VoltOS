# VoltOS

Sistema embarcado de gerenciamento e monitoramento de bateria (**BMS — Battery Management System**) desenvolvido sobre a plataforma **STM32F401RE** e **FreeRTOS**.

O projeto tem como objetivo demonstrar, de forma prática, a implementação de um sistema embarcado de tempo real capaz de monitorar parâmetros elétricos e térmicos de uma bateria, detectar condições anormais, executar mecanismos de proteção e sinalizar falhas.

---

## 🎯 Objetivos

O VoltOS foi desenvolvido para demonstrar conceitos fundamentais de sistemas embarcados e sistemas operacionais de tempo real, incluindo:

* Monitoramento de tensão da bateria;
* Monitoramento de corrente;
* Monitoramento de temperatura;
* Estimativa do estado de carga (**SOC — State of Charge**);
* Detecção de condições de operação anormais;
* Detecção e classificação de falhas;
* Injeção controlada de falhas para testes;
* Execução concorrente utilizando **FreeRTOS**;
* Comunicação entre tarefas através de filas;
* Priorização de tarefas;
* Sinalização visual de estados e falhas;
* Simulação de sensores para validação do sistema.

---

## 🔧 Hardware

| Componente  | Descrição       |
| ----------- | --------------- |
| MCU         | **STM32F401RE** |
| Arquitetura | ARM Cortex-M4   |
| Plataforma  | STM32 Nucleo    |
| RTOS        | FreeRTOS        |
| Comunicação | UART            |
| Indicadores | LEDs            |
| Sensores    | Simulados       |

### Placa utilizada

**STM32F401RE**

O microcontrolador é responsável pela execução do sistema operacional de tempo real, processamento dos dados dos sensores, avaliação das condições de segurança e gerenciamento das tarefas do BMS.

---

## 🧠 Arquitetura

O VoltOS utiliza uma arquitetura baseada em tarefas independentes executadas pelo FreeRTOS.

De forma simplificada:

```text
                    ┌──────────────────────┐
                    │      VoltOS BMS      │
                    └──────────┬───────────┘
                               │
             ┌─────────────────┼─────────────────┐
             │                 │                 │
             ▼                 ▼                 ▼
      ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
      │   Tensão    │   │   Corrente  │   │ Temperatura │
      │    Task     │   │     Task    │   │     Task    │
      └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
             │                 │                 │
             └─────────────────┼─────────────────┘
                               │
                         FreeRTOS Queues
                               │
                               ▼
                    ┌──────────────────────┐
                    │     Safety Task      │
                    │                      │
                    │ • Validação          │
                    │ • Proteção           │
                    │ • Classificação      │
                    │ • Estado do BMS      │
                    └──────────┬───────────┘
                               │
                 ┌─────────────┼─────────────┐
                 │             │             │
                 ▼             ▼             ▼
              LEDs          Monitor       Sistema
                            / UART         Seguro
```

---

## ⚙️ Tarefas do sistema

O sistema é dividido em tarefas com responsabilidades específicas.

### Monitor Task

Responsável por apresentar informações do sistema através da interface serial.

Exibe informações como:

* Tensão;
* Corrente;
* Temperatura;
* SOC;
* Estado atual do BMS;
* Falhas detectadas.

---

### Voltage Task

Responsável pela aquisição e/ou simulação da tensão da bateria.

Os dados são enviados para a fila correspondente para posterior processamento pela tarefa de segurança.

---

### Current Task

Responsável pela aquisição e/ou simulação da corrente da bateria.

Os valores são disponibilizados para o sistema através de uma fila do FreeRTOS.

---

### Temperature Task

Responsável pelo monitoramento da temperatura.

A tarefa permite identificar condições de sobretemperatura que podem comprometer a segurança do sistema.

---

### Safety Task

É a principal tarefa de proteção do VoltOS.

Ela recebe os dados provenientes das demais tarefas e avalia as condições de operação da bateria.

Entre suas responsabilidades estão:

* Verificar limites de tensão;
* Verificar limites de corrente;
* Verificar limites de temperatura;
* Avaliar o estado geral do BMS;
* Identificar falhas;
* Classificar a severidade da condição;
* Atualizar os indicadores de segurança;
* Enviar o estado consolidado para o monitoramento.

---

### Fault Injection Task

Responsável pela injeção controlada de falhas durante os testes.

A funcionalidade permite simular condições que seriam difíceis ou perigosas de reproduzir fisicamente.

Exemplos:

* Sobretensão;
* Subtensão;
* Sobrecorrente;
* Sobretemperatura.

A injeção de falhas não substitui a lógica de segurança. A falha é introduzida no sistema e posteriormente detectada pela própria arquitetura do BMS.

Isso permite testar o comportamento real do mecanismo de proteção.

---

## 🚨 Sistema de proteção

O VoltOS possui limites de proteção para os principais parâmetros monitorados.

### Tensão

```text
Sobretensão  → Voltage > limite máximo
Subtensão    → Voltage < limite mínimo
```

### Corrente

```text
Sobrecorrente → Current > limite máximo
```

### Temperatura

```text
Sobretemperatura → Temperature > limite máximo
```

Quando uma condição crítica é identificada, o sistema altera seu estado e aciona os mecanismos de sinalização correspondentes.

---

## 💡 Sinalização de falhas

Cada tipo de falha pode possuir um indicador visual específico.

| Indicador | Falha           |
| --------- | --------------- |
| LED geral | Estado crítico  |
| LED OV    | Overvoltage     |
| LED UV    | Undervoltage    |
| LED OC    | Overcurrent     |
| LED OT    | Overtemperature |

A responsabilidade pela ativação dos indicadores pertence à lógica de segurança, evitando que a tarefa responsável pela injeção de falhas manipule diretamente o estado de proteção.

---

## 🧪 Injeção de falhas

Para validar o comportamento do sistema, o VoltOS possui um mecanismo de **Fault Injection**.

Um ciclo de testes pode seguir, por exemplo:

```text
NORMAL
   ↓
OVERVOLTAGE
   ↓
NORMAL
   ↓
UNDERVOLTAGE
   ↓
NORMAL
   ↓
OVERCURRENT
   ↓
NORMAL
   ↓
OVERTEMPERATURE
   ↓
NORMAL
```

Durante cada etapa, a tarefa de injeção modifica o cenário de operação e permite verificar se o restante do sistema consegue identificar corretamente a condição.

### Objetivo

A injeção de falhas permite testar:

* Detecção;
* Classificação;
* Tempo de resposta;
* Comunicação entre tarefas;
* Sinalização;
* Comportamento do sistema em condições críticas.

---

## 📊 Estados do BMS

O sistema utiliza diferentes estados para representar a condição operacional da bateria.

```text
NORMAL
   │
   ├── Warning
   │
   └── Critical
```

### NORMAL

Todos os parâmetros estão dentro das condições esperadas.

### WARNING

Um ou mais parâmetros estão se aproximando de uma condição de proteção.

### CRITICAL

Um parâmetro ultrapassou um limite crítico e uma condição de falha foi identificada.

---

## 📨 Comunicação entre tarefas

A comunicação entre as tarefas é realizada utilizando **Queues do FreeRTOS**.

Exemplo conceitual:

```text
Voltage Task
      │
      ▼
Voltage Queue
      │
      │
Current Task
      │
      ▼
Current Queue
      │
      │
Temperature Task
      │
      ▼
Temperature Queue
      │
      ▼
Safety Task
      │
      ▼
BMS State Queue
      │
      ▼
Monitor Task
```

Essa abordagem evita a necessidade de compartilhar diretamente os dados entre as tarefas e permite utilizar os mecanismos de sincronização fornecidos pelo FreeRTOS.

---

## ⏱️ Sistema de tempo real

O VoltOS utiliza o FreeRTOS para controlar a execução concorrente das tarefas.

Cada tarefa possui uma prioridade definida de acordo com sua importância para o sistema.

Um exemplo de hierarquia é:

| Task                 | Prioridade | Responsabilidade |
| -------------------- | ---------: | ---------------- |
| Safety Task          |       Alta | Proteção         |
| Fault Injection Task | Média-alta | Testes           |
| Voltage Task         |      Média | Monitoramento    |
| Current Task         |      Média | Monitoramento    |
| Temperature Task     |      Média | Monitoramento    |
| Monitor Task         |      Baixa | Interface        |

A prioridade elevada da **Safety Task** garante que o processamento das condições de segurança tenha precedência sobre tarefas de menor criticidade.

---

## 🖥️ Monitoramento

As informações do sistema podem ser acompanhadas através da comunicação serial.

Exemplo:

```text
==============================
        VoltOS BMS
==============================

Voltage     : 48.20 V
Current     : 10.50 A
Temperature : 32.50 C
SOC         : 87.30 %

Status      : NORMAL

OV : 0
UV : 0
OC : 0
OT : 0
```

Durante uma falha:

```text
==============================
        VoltOS BMS
==============================

Voltage     : 55.00 V
Current     : 10.50 A
Temperature : 32.50 C
SOC         : 87.20 %

Status      : CRITICAL

OV : 1
UV : 0
OC : 0
OT : 0

FAULT: OVERVOLTAGE
```

---

## 🧰 Tecnologias

* **C**
* **ARM Cortex-M4**
* **STM32F401RE**
* **FreeRTOS**
* **STM32 HAL**
* **UART**
* **GPIO**
* **Queues**
* **Tasks**
* **RTOS Scheduling**

---

## 📁 Estrutura do projeto

Uma possível organização do projeto:

```text
VoltOS/
│
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   └── ...
│   │
│   └── Src/
│       ├── main.c
│       └── ...
│
├── Middlewares/
│   └── Third_Party/
│       └── FreeRTOS/
│
├── Drivers/
│
├── README.md
└── ...
```

A estrutura pode variar de acordo com a ferramenta utilizada para geração e compilação do firmware.

---

## ▶️ Execução

### Pré-requisitos

Para executar o projeto fisicamente, é necessário possuir:

* STM32F401RE;
* Cabo USB;
* Ambiente de desenvolvimento STM32;
* Toolchain ARM GCC;
* FreeRTOS;
* Terminal serial.

### Fluxo de execução

```text
Build
  ↓
Flash
  ↓
STM32F401RE
  ↓
FreeRTOS
  ↓
Criação das Tasks
  ↓
Inicialização das Queues
  ↓
Scheduler
  ↓
Monitoramento
  ↓
Detecção de falhas
```

---

## 🔬 Objetivo acadêmico/técnico

Além de representar um BMS simplificado, o VoltOS foi projetado como um laboratório prático para explorar conceitos de **sistemas embarcados de tempo real**.

O projeto permite estudar na prática:

* Sistemas operacionais de tempo real;
* Escalonamento preemptivo;
* Prioridades;
* Comunicação entre tarefas;
* Filas;
* Sincronização;
* Monitoramento de sensores;
* Sistemas de proteção;
* Injeção de falhas;
* Sistemas críticos;
* Programação embarcada em C;
* Arquitetura ARM Cortex-M.

---

## 🚧 Status do projeto

**Em desenvolvimento.**

Funcionalidades atualmente contempladas:

* [x] Estrutura básica do BMS
* [x] FreeRTOS
* [x] Tarefas independentes
* [x] Filas para comunicação
* [x] Monitoramento de tensão
* [x] Monitoramento de corrente
* [x] Monitoramento de temperatura
* [x] Monitoramento de SOC
* [x] Detecção de falhas
* [x] Injeção de falhas
* [x] Estados NORMAL / WARNING / CRITICAL
* [x] Sinalização visual
* [x] Monitoramento via UART
* [ ] Persistência de eventos
* [ ] Telemetria

--- 

## 🎥 Demo

Abaixo está uma demonstração do **VoltOS** em execução, mostrando a simulação do BMS, o funcionamento das tarefas do FreeRTOS, o monitoramento dos parâmetros e a detecção das falhas injetadas.

<video src="./simulation/simulationVoltOS.mp4" controls width="800">
  Seu navegador não suporta a reprodução de vídeos.
</video>

### 📺 Simulação

O vídeo demonstra o funcionamento do sistema durante a execução da simulação, incluindo:

* Inicialização do sistema;
* Execução das tarefas do FreeRTOS;
* Monitoramento de tensão;
* Monitoramento de corrente;
* Monitoramento de temperatura;
* Atualização do SOC;
* Comunicação entre tarefas através de queues;
* Injeção de falhas;
* Detecção das condições críticas;
* Alteração do estado do BMS;
* Acionamento dos LEDs correspondentes às falhas;
* Recuperação do sistema após as falhas.
