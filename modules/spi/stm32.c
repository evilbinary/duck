/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

/******************************************************************************
 * STM32F4 SPI1硬件配置
 * 
 * SPI1引脚映射 (Alternate Function AF5)：
 *   PA5  - SPI1_SCK  - 时钟输出
 *   PA6  - SPI1_MISO - 数据输入(本LCD不用)
 *   PA7  - SPI1_MOSI - 数据输出
 *   PA4  - SPI1_NSS  - 硬件片选(本驱动不用，使用PA8手动控制)
 * 
 * LCD控制引脚 (GPIO手动控制)：
 *   PA8  - CS  - 片选(软件控制，避免与MISO冲突)
 *   PA11 - DC  - 数据/命令选择
 *   PB7  - RES - 复位
 * 
 * 配置参数：
 *   - 模式：主机模式 (SPI_MODE_MASTER)
 *   - 时钟极性：低电平 (CPOL=LOW)
 *   - 时钟相位：第一个边沿 (CPHA=1EDGE) → SPI Mode 0
 *   - 数据位宽：8位 (SPI_DATASIZE_8BIT)
 *   - 波特率：PCLK/16 ≈ 5.25MHz @84MHz
 *   - 首位：MSB先传
 ******************************************************************************/
#include "platform/stm32f4xx/gpio.h"
#include "platform/stm32f4xx/stm32f4xx_hal.h"
#include "spi.h"

// #define SPI_DMA 1

static SPI_HandleTypeDef hspi1;

static u32 stm32_spi_read(spi_t* spi, u32* data, u32 count) {
  if (data == NULL || count <= 0) {
    return 0;
  }
#ifdef SPI_DMA
  HAL_StatusTypeDef errorcode =
      HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)data, count);
#else
  HAL_StatusTypeDef errorcode =
      HAL_SPI_Receive(&hspi1, (uint8_t*)data, count, HAL_MAX_DELAY);
#endif
  if (errorcode != HAL_OK) {
    return -1;
  }
  return count;
}

static u32 stm32_spi_write(spi_t* spi, u32* data, u32 count) {
  if (data == NULL || count <= 0) {
    return 0;
  }
  
  // Check if SPI needs reinitialization (if SCK is high, GPIO config may be lost)
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_SET) {
    // SCK is high, which shouldn't happen in CPOL=LOW mode
    // Reconfigure SPI
    HAL_SPI_Init(&hspi1);
  }
  
#ifdef SPI_DMA
  HAL_StatusTypeDef errorcode =
      HAL_SPI_Transmit_DMA(&hspi1, (uint8_t*)data, count);
#else
  HAL_StatusTypeDef errorcode =
      HAL_SPI_Transmit(&hspi1, (uint8_t*)data, count, HAL_MAX_DELAY);
#endif
  if (errorcode != HAL_OK) {
    return -1;
  }
  
  return count;
}

static void stm32_spi_init() {
  // GPIO configuration is done in HAL_SPI_MspInit in stm32f4xx_hal_msp.c
  // Just enable clocks here
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_SPI1_CLK_ENABLE();
  
  // Configure PA8 as GPIO output for CS (manual control)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);  // CS high initially


  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;  // ~5.25MHz for ST7735 (safer)
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
    log_error("spi init error\n");
  } else {
    log_debug("spi init success (Mode 0, prescaler 8)\n");
  }
}

int spi_init_device(device_t* dev) {
  spi_t* spi = kmalloc(sizeof(spi_t),DEFAULT_TYPE);
  dev->data = spi;

  spi->inited = 0;
  spi->read = stm32_spi_read;
  spi->write = stm32_spi_write;
  stm32_spi_init();
  spi->inited = 1;
  return 0;
}