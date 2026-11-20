/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "scheduler.h"
#include <ctype.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CALLBACK_STOP_MS 490u
#define SCH_REPORT_ERRORS 1
#define ERROR_STATUS_MS 2000u
#define WATCHDOG_DEMO_TICKS 50u
/* Enable for a real timeout test on hardware. Proteus 8.10 crashes on reset. */
#define WATCHDOG_ALLOW_TIMEOUT 0
#define TICK_PROBE_Pin GPIO_PIN_0
#define SLEEP_PROBE_Pin GPIO_PIN_1
#define WDG_PROBE_Pin GPIO_PIN_2
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t cancel_id;
static uint8_t error_code;
static uint32_t error_started;
static bool sleep_enabled = true;
static bool show_ticks = true;
static uint32_t tick_callback_id = SCH_INVALID_ID;
static volatile bool watchdog_test;
static volatile uint32_t watchdog_missed_ticks;

enum {
    ERROR_SCH_NONE,
    ERROR_SCH_TOO_MANY_TASKS,
    ERROR_SCH_CANNOT_DELETE_TASK
};

static void uart_put(char value)
{
    uint8_t byte = (uint8_t)value;
    if (HAL_UART_Transmit(&huart1, &byte, 1, 10) != HAL_OK) {
        Error_Handler();
    }
}

static void uart_text(const char *text)
{
    while (*text != '\0') {
        uart_put(*text++);
    }
}

static void log_time(char event)
{
    uint32_t time_ms = get_time();
    char digits[10];
    unsigned count = 0;

    bool led_event = event >= '1' && event <= '5';
    if (!led_event) {
        uart_put(event);
        uart_put(' ');
    }
    do {
        digits[count++] = (char)('0' + time_ms % 10u);
        time_ms /= 10u;
    } while (time_ms != 0u);
    while (count != 0u) {
        uart_put(digits[--count]);
    }
    if (led_event) {
        uart_text(" LED");
        uart_put(event);
    }
    uart_text("\r\n");
}

static void tick_callback(void)
{
    if (get_time() >= CALLBACK_STOP_MS) {
        if (SCH_Delete_Task(tick_callback_id)) {
            tick_callback_id = SCH_INVALID_ID;
            HAL_GPIO_WritePin(STATUS_GPIO_Port, STATUS_Pin, GPIO_PIN_RESET);
            uart_text("STOP 490 ms (10ms callback)\r\n");
            return;
        }
        Error_Handler();
        return;
    }
    if (show_ticks) {
        log_time('T');
    }
}

static void Led_Display(void)
{
    HAL_GPIO_TogglePin(TASK_1000_GPIO_Port, TASK_1000_Pin);
    log_time('2');
}

static void task_500(void)
{
    HAL_GPIO_TogglePin(TASK_500_GPIO_Port, TASK_500_Pin);
    log_time('1');
}

static void task_1500(void)
{
    HAL_GPIO_TogglePin(TASK_1500_GPIO_Port, TASK_1500_Pin);
    log_time('3');
}

static void task_2000(void)
{
    HAL_GPIO_TogglePin(TASK_2000_GPIO_Port, TASK_2000_Pin);
    log_time('4');
}

static void task_2500(void)
{
    HAL_GPIO_TogglePin(TASK_2500_GPIO_Port, TASK_2500_Pin);
    log_time('5');
}

static void cancelled(void) { log_time('X'); }

static void SCH_Set_Error(uint8_t code)
{
    error_code = code;
    error_started = get_time();
}

static void SCH_Report_Status(void)
{
#if SCH_REPORT_ERRORS
    static uint8_t last_error;
    if (error_code != ERROR_SCH_NONE && get_time() - error_started >= ERROR_STATUS_MS) {
        error_code = ERROR_SCH_NONE;
    }
    if (error_code != last_error) {
        last_error = error_code;
        if (error_code == ERROR_SCH_CANNOT_DELETE_TASK) {
            uart_text("ERROR 2: invalid task ID\r\n");
        } else if (error_code == ERROR_SCH_TOO_MANY_TASKS) {
            uart_text("ERROR 1: scheduler rejected task\r\n");
        } else {
            uart_text("STATUS: clear\r\n");
        }
    }
#endif
    HAL_GPIO_WritePin(STATUS_GPIO_Port, STATUS_Pin,
        (tick_callback_id != SCH_INVALID_ID || error_code != ERROR_SCH_NONE || watchdog_missed_ticks > 3u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void SCH_Go_To_Sleep(void)
{
    if (!SCH_Task_Ready() && !(USART1->SR & USART_SR_RXNE)) {
        /* Leave SysTick running. It also wakes us if a timer interrupt
           arrives between the ready check and the sleep instruction. */
        HAL_GPIO_WritePin(GPIOA, SLEEP_PROBE_Pin, GPIO_PIN_SET);
        __DSB();
        __WFI();
        HAL_GPIO_WritePin(GPIOA, SLEEP_PROBE_Pin, GPIO_PIN_RESET);
    }
}

static void Watchdog_Refresh(void)
{
    if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK) {
        Error_Handler();
    }
    HAL_GPIO_TogglePin(GPIOA, WDG_PROBE_Pin);
}

static bool Is_Watchdog_Reset(void)
{
    return __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET;
}

static void Watchdog_Counting(void)
{
    if (watchdog_missed_ticks < WATCHDOG_DEMO_TICKS) {
        ++watchdog_missed_ticks;
    }
}

static void Reset_Watchdog_Counting(void)
{
    watchdog_missed_ticks = 0;
}

static void Terminal_Commands(void)
{
    if (!(USART1->SR & USART_SR_RXNE)) {
        return;
    }
    switch (tolower((uint8_t)USART1->DR)) {
    case 't':
        show_ticks = !show_ticks;
        uart_text(show_ticks ? "TICK LOG: on\r\n" : "TICK LOG: off\r\n");
        break;
    case 'e':
        if (!SCH_Delete_Task(SCH_INVALID_ID)) {
            SCH_Set_Error(ERROR_SCH_CANNOT_DELETE_TASK);
        }
        break;
    case 's':
        sleep_enabled = !sleep_enabled;
        uart_text(sleep_enabled ? "SLEEP: on\r\n" : "SLEEP: off\r\n");
        break;
    case 'w':
        if (!watchdog_test) {
            Reset_Watchdog_Counting();
            uart_text("WATCHDOG: withholding refresh\r\n");
            watchdog_test = true;
        }
        break;
    default:
        break;
    }
}

static void cancel_task(void)
{
    if (!SCH_Delete_Task(cancel_id)) {
        SCH_Set_Error(ERROR_SCH_CANNOT_DELETE_TASK);
    }
}

static uint32_t register_task(void (*function)(void), uint32_t delay_ms,
                              uint32_t period_ms)
{
    uint32_t id = SCH_Add_Task(function, delay_ms / SCH_TICK_MS,
                              period_ms / SCH_TICK_MS);
    if (id == SCH_INVALID_ID) {
        SCH_Set_Error(ERROR_SCH_TOO_MANY_TASKS);
    }
    return id;
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
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
    GPIO_InitTypeDef probes = {0};
    probes.Pin = TICK_PROBE_Pin | SLEEP_PROBE_Pin | WDG_PROBE_Pin;
    probes.Mode = GPIO_MODE_OUTPUT_PP;
    probes.Pull = GPIO_NOPULL;
    probes.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOA, probes.Pin, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOA, &probes);
    uart_text("LAB4 scheduler | time in ms\r\n");
    if (Is_Watchdog_Reset()) {
        uart_text("WATCHDOG: reset detected\r\n");
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();
    Reset_Watchdog_Counting();
    uart_text("Keys: t=ticks, e=error, s=sleep, w=watchdog\r\n");
    SCH_Init();
    tick_callback_id = register_task(tick_callback, SCH_TICK_MS, SCH_TICK_MS);
    SCH_Report_Status();
    register_task(task_500, 500, 500);
    register_task(Led_Display, 1000, 1000);
    register_task(task_1500, 1500, 1500);
    register_task(task_2000, 2000, 2000);
    register_task(task_2500, 2500, 2500);

    /* Intentionally register these out of due order. */
    cancel_id = register_task(cancelled, 700, 0);
    register_task(cancel_task, 300, 0);
    /* CubeMX initializes TIM2 with an update event. Start on a full 10 ms interval. */
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
        Error_Handler();
    }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    SCH_Dispatch_Tasks();
    Terminal_Commands();
    if (!WATCHDOG_ALLOW_TIMEOUT && watchdog_test && watchdog_missed_ticks >= WATCHDOG_DEMO_TICKS) {
        Reset_Watchdog_Counting();
        watchdog_test = false;
        uart_text("WATCHDOG: refresh resumed\r\n");
    }
    SCH_Report_Status();
    if (sleep_enabled) {
        SCH_Go_To_Sleep();
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 799;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 99;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, TASK_500_Pin|TASK_1000_Pin|TASK_1500_Pin|TASK_2000_Pin
                          |TASK_2500_Pin|STATUS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : TASK_500_Pin TASK_1000_Pin TASK_1500_Pin TASK_2000_Pin
                           TASK_2500_Pin STATUS_Pin */
  GPIO_InitStruct.Pin = TASK_500_Pin|TASK_1000_Pin|TASK_1500_Pin|TASK_2000_Pin
                          |TASK_2500_Pin|STATUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer->Instance == TIM2) {
        HAL_GPIO_TogglePin(GPIOA, TICK_PROBE_Pin);
        SCH_Update();
        if (watchdog_test) {
            Watchdog_Counting();
        } else {
            Watchdog_Refresh();
        }
    }
}
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

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
