#ifndef PL181_H
#define PL181_H


#include "sdhci.h"


//https://developer.arm.com/documentation/ddi0172/latest


#define CONFIG_ARM_PL180_MMCI_CLOCK_FREQ 6250000
#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_136 (1 << 1)    /* 136 bit response */
#define MMC_RSP_CRC (1 << 2)    /* expect valid crc */
#define MMC_RSP_BUSY  (1 << 3)    /* card may send busy */
#define MMC_RSP_OPCODE  (1 << 4)    /* response contains opcode */

#define MMC_RSP_NONE  (0)
#define MMC_RSP_R1  (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R1b (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE| \
		MMC_RSP_BUSY)
#define MMC_RSP_R2  (MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC)
#define MMC_RSP_R3  (MMC_RSP_PRESENT)
#define MMC_RSP_R4  (MMC_RSP_PRESENT)
#define MMC_RSP_R5  (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R6  (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R7  (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)

/* SDI Status register bits */
#define SDI_STA_CCRCFAIL  0x00000001
#define SDI_STA_DCRCFAIL  0x00000002
#define SDI_STA_CTIMEOUT  0x00000004
#define SDI_STA_DTIMEOUT  0x00000008
#define SDI_STA_TXUNDERR  0x00000010
#define SDI_STA_RXOVERR   0x00000020
#define SDI_STA_CMDREND   0x00000040
#define SDI_STA_CMDSENT   0x00000080
#define SDI_STA_DATAEND   0x00000100
#define SDI_STA_STBITERR  0x00000200
#define SDI_STA_DBCKEND   0x00000400
#define SDI_STA_CMDACT    0x00000800
#define SDI_STA_TXACT   0x00001000
#define SDI_STA_RXACT   0x00002000
#define SDI_STA_TXFIFOBW  0x00004000
#define SDI_STA_RXFIFOBR  0x00008000
#define SDI_STA_TXFIFOF   0x00010000
#define SDI_STA_RXFIFOF   0x00020000
#define SDI_STA_TXFIFOE   0x00040000
#define SDI_STA_RXFIFOE   0x00080000
#define SDI_STA_TXDAVL    0x00100000
#define SDI_STA_RXDAVL    0x00200000
#define SDI_STA_SDIOIT    0x00400000
#define SDI_STA_CEATAEND  0x00800000
#define SDI_STA_CARDBUSY  0x01000000
#define SDI_STA_BOOTMODE  0x02000000
#define SDI_STA_BOOTACKERR  0x04000000
#define SDI_STA_BOOTACKTIMEOUT  0x08000000
#define SDI_STA_RSTNEND   0x10000000

/******* The PrimeCell MCI registers are shown in Table 3-1. *******/

#define POWER         0x00
#define CLOCK         0x04
#define ARGUMENT      0x08
#define COMMAND       0x0c
#define RESPCOMMAND   0x10
#define RESPONSE0     0x14
#define RESPONSE1     0x18
#define RESPONSE2     0x1c
#define RESPONSE3     0x20
#define DATATIMER     0x24
#define DATALENGTH    0x28
#define DATACTRL      0x2c
#define DATACOUNT     0x30
#define STATUS        0x34
#define STATUS_CLEAR  0x38
#define MASK0         0x3c
#define MASK1         0x40
#define CARD_SELECT   0x44
#define FIFO_COUNT    0x48
#define FIFO          0x80

#define SD_RCA  0x45670000

#define SD_BASE  0x10005000





#endif