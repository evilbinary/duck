#ifndef __ST7735_H__
#define __ST7735_H__

/******************************************************************************
 * ST7735 LCD与STM32F4连接图
 * 
 * ST7735(128x128)          STM32F401CC
 * ┌─────────────────┐      ┌─────────────────┐
 * │                 │      │                 │
 * │ GND    ─────────┼──────┼─ GND            │
 * │ VCC    ─────────┼──────┼─ 3.3V           │
 * │ LED    ─────────┼──────┼─ 3.3V (背光)    │
 * │                 │      │                 │
 * │ CLK    ─────────┼──────┼─ PA5 (SPI1_SCK) │ ← 时钟
 * │ DIN    ─────────┼──────┼─ PA7 (SPI1_MOSI)│ ← 数据输出
 * │                 │      │                 │
 * │ CS     ─────────┼──────┼─ PA8 (GPIO)     │ ← 片选(手动控制)
 * │ DC     ─────────┼──────┼─ PA11 (GPIO)    │ ← 命令/数据选择
 * │ RST    ─────────┼──────┼─ PB7 (GPIO)     │ ← 复位
 * │                 │      │                 │
 * └─────────────────┘      └─────────────────┘
 * 
 * 注意：
 * 1. CS引脚使用PA8，避免与SPI1_MISO(PB6)冲突
 * 2. SPI模式：Mode 0 (CPOL=LOW, CPHA=1EDGE)
 * 3. 时钟频率：~5.25MHz (SPI_BAUDRATEPRESCALER_16)
 * 4. 背光LED直接接3.3V或通过限流电阻连接
 ******************************************************************************/

// GPIO引脚定义
#define ST7735_RES_Pin       GPIO_PIN_7      // PB7 - 复位
#define ST7735_RES_GPIO_Port GPIOB
#define ST7735_CS_Pin        GPIO_PIN_8      // PA8 - 片选(注意：原PB6与SPI1_MISO冲突)
#define ST7735_CS_GPIO_Port  GPIOA
#define ST7735_DC_Pin        GPIO_PIN_11     // PA11 - 数据/命令选择
#define ST7735_DC_GPIO_Port  GPIOA

#endif /* __ST7735_H__ */