/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SW_90_Pin GPIO_PIN_13
#define SW_90_GPIO_Port GPIOC
#define SW_2_Pin GPIO_PIN_15
#define SW_2_GPIO_Port GPIOC
#define PHOTO_0_Pin GPIO_PIN_0
#define PHOTO_0_GPIO_Port GPIOA
#define PHOTO_1_Pin GPIO_PIN_1
#define PHOTO_1_GPIO_Port GPIOA
#define U_BTN_Pin GPIO_PIN_2
#define U_BTN_GPIO_Port GPIOA
#define BATT_V_Pin GPIO_PIN_3
#define BATT_V_GPIO_Port GPIOA
#define SPI_IMU_CS_Pin GPIO_PIN_4
#define SPI_IMU_CS_GPIO_Port GPIOA
#define IMU_FSYNC_Pin GPIO_PIN_5
#define IMU_FSYNC_GPIO_Port GPIOC
#define DIP_0_Pin GPIO_PIN_0
#define DIP_0_GPIO_Port GPIOB
#define DIP_1_Pin GPIO_PIN_1
#define DIP_1_GPIO_Port GPIOB
#define DIP_2_Pin GPIO_PIN_2
#define DIP_2_GPIO_Port GPIOB
#define DIP_3_Pin GPIO_PIN_10
#define DIP_3_GPIO_Port GPIOB
#define LED_1_Pin GPIO_PIN_11
#define LED_1_GPIO_Port GPIOB
#define CM4_CS_Pin GPIO_PIN_6
#define CM4_CS_GPIO_Port GPIOC
#define LED_G_Pin GPIO_PIN_7
#define LED_G_GPIO_Port GPIOC
#define LED_R_Pin GPIO_PIN_8
#define LED_R_GPIO_Port GPIOC
#define LED_B_Pin GPIO_PIN_9
#define LED_B_GPIO_Port GPIOC
#define BUZZER_Pin GPIO_PIN_8
#define BUZZER_GPIO_Port GPIOA
#define LED_3_Pin GPIO_PIN_9
#define LED_3_GPIO_Port GPIOA
#define LED_2_Pin GPIO_PIN_10
#define LED_2_GPIO_Port GPIOA
#define LED_0_Pin GPIO_PIN_12
#define LED_0_GPIO_Port GPIOC
#define ESC_PWM_Pin GPIO_PIN_3
#define ESC_PWM_GPIO_Port GPIOB
#define INTERRUPTER_OUT_Pin GPIO_PIN_5
#define INTERRUPTER_OUT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
