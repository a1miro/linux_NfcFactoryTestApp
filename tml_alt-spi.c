/*
*         Copyright (c), NXP Semiconductors Caen / France
*
*                     (C)NXP Semiconductors
*       All rights are reserved. Reproduction in whole or in part is
*      prohibited without the written consent of the copyright owner.
*  NXP reserves the right to make changes without notice at any time.
* NXP makes no warranty, expressed, implied or statutory, including but
* not limited to any implied warranty of merchantability or fitness for any
*particular purpose, or that the use will not infringe any third party patent,
* copyright or trademark. NXP must not be liable for any loss or damage
*                          arising from its use.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <tml.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

#define SPI_BUS         "/dev/spidev6.0"
#define SPI_MODE	SPI_MODE_0;
#define SPI_BITS	8;
#define SPI_SPEED       1000000 // 1 MHz

#define IRQ_GPIO_CHIP_PATH   "/dev/gpiochip0"
#define IRQ_GPIO_OFFSET      10
#define VEN_GPIO_CHIP_PATH   "/dev/gpiochip4"
#define VEN_GPIO_OFFSET      6

struct gpio_line_desc {
    const char *chipPath;
    unsigned int offset;
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;
};

static struct gpio_line_desc gInterruptLine = {
    .chipPath = IRQ_GPIO_CHIP_PATH,
    .offset = IRQ_GPIO_OFFSET,
    .chip = NULL,
    .request = NULL,
};

static struct gpio_line_desc gEnableLine = {
    .chipPath = VEN_GPIO_CHIP_PATH,
    .offset = VEN_GPIO_OFFSET,
    .chip = NULL,
    .request = NULL,
};

static struct gpiod_line_request *requestLine(struct gpiod_chip *chip,
                                              unsigned int offset,
                                              enum gpiod_line_direction direction,
                                              enum gpiod_line_value outputValue,
                                              int enableEdgeDetect)
{
    struct gpiod_line_settings *settings = NULL;
    struct gpiod_line_config *lineCfg = NULL;
    struct gpiod_request_config *reqCfg = NULL;
    struct gpiod_line_request *request = NULL;

    settings = gpiod_line_settings_new();
    lineCfg = gpiod_line_config_new();
    reqCfg = gpiod_request_config_new();
    if (!settings || !lineCfg || !reqCfg) goto done;

    if (gpiod_line_settings_set_direction(settings, direction) < 0) goto done;
    if (enableEdgeDetect) {
        if (gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING) < 0) goto done;
    }
    if (direction == GPIOD_LINE_DIRECTION_OUTPUT &&
        gpiod_line_settings_set_output_value(settings, outputValue) < 0) {
        goto done;
    }
    if (gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings) < 0) goto done;

    gpiod_request_config_set_consumer(reqCfg, "NfcFactoryTestApp");
    request = gpiod_chip_request_lines(chip, reqCfg, lineCfg);

done:
    gpiod_request_config_free(reqCfg);
    gpiod_line_config_free(lineCfg);
    gpiod_line_settings_free(settings);
    return request;
}

static int openLine(struct gpio_line_desc *line,
                    enum gpiod_line_direction direction,
                    enum gpiod_line_value outputValue,
                    int enableEdgeDetect)
{
    line->chip = gpiod_chip_open(line->chipPath);
    if (!line->chip) {
        perror("gpiod_chip_open");
        return -1;
    }

    line->request = requestLine(line->chip, line->offset, direction, outputValue, enableEdgeDetect);
    if (!line->request) {
        perror("gpiod request line");
        gpiod_chip_close(line->chip);
        line->chip = NULL;
        return -1;
    }

    return 0;
}

static void closeLine(struct gpio_line_desc *line)
{
    if (line->request) {
        gpiod_line_request_release(line->request);
        line->request = NULL;
    }
    if (line->chip) {
        gpiod_chip_close(line->chip);
        line->chip = NULL;
    }
}

static void closeGpio(void)
{
    closeLine(&gEnableLine);
    closeLine(&gInterruptLine);
}

static int openGpio(void)
{
    if (openLine(&gInterruptLine, GPIOD_LINE_DIRECTION_INPUT,
                 GPIOD_LINE_VALUE_INACTIVE, 1) < 0) {
        goto error;
    }

    if (openLine(&gEnableLine, GPIOD_LINE_DIRECTION_OUTPUT,
                 GPIOD_LINE_VALUE_INACTIVE, 0) < 0) {
        goto error;
    }

    return 0;

error:
    closeGpio();
    return -1;
}

static int pnGetint( void ) {
    int val, ret;

    if (!gInterruptLine.request) return -1;

    /* Wait up to 2 seconds for an edge event (rising edge) */
    ret = gpiod_line_request_wait_edge_events(gInterruptLine.request, 2000000000LL);
    if (ret < 0) {
        perror("gpiod_line_request_wait_edge_events");
        return -1;
    }
    if (ret == 0) {
        /* Timeout - no edge detected */
        return 0;
    }

    /* Edge detected, read the current value to confirm */
    val = gpiod_line_request_get_value(gInterruptLine.request, gInterruptLine.offset);
    if (val == GPIOD_LINE_VALUE_ERROR) {
        perror("gpiod_line_get_value(IRQ)");
        return -1;
    }
    return (val == GPIOD_LINE_VALUE_ACTIVE);
}

static int SpiRead(int pDevHandle, char* pBuffer, int nBytesToRead) {
    int numRead = 0;
    struct spi_ioc_transfer spi[2];
    char buf = 0xFF;
    memset(spi, 0x0, sizeof(spi));
    spi[0].tx_buf = (unsigned long)&buf;
    spi[0].rx_buf = (unsigned long)NULL;
    spi[0].len = 1;
    spi[0].delay_usecs = 0;
    spi[0].speed_hz = SPI_SPEED;
    spi[0].bits_per_word = SPI_BITS;
    spi[0].cs_change = 1;
    spi[0].tx_nbits = 0;
    spi[0].rx_nbits = 0;
    spi[1].tx_buf = (unsigned long)NULL;
    spi[1].rx_buf = (unsigned long)pBuffer;
    spi[1].len = nBytesToRead;
    spi[1].delay_usecs = 0;
    spi[1].speed_hz = SPI_SPEED;
    spi[1].bits_per_word = SPI_BITS;
    spi[1].cs_change = 1;
    spi[1].tx_nbits = 0;
    spi[1].rx_nbits = 0;
    numRead = ioctl(pDevHandle, SPI_IOC_MESSAGE(2), &spi);
    if (numRead > 0) numRead -= 1;
    return numRead;
}

int tml_open(int * handle)
{
    unsigned char spi_mode = SPI_MODE;
    unsigned char spi_bitsPerWord = SPI_BITS;
    static unsigned int speed = SPI_SPEED;
    *handle = -1;

    if (openGpio() < 0) goto error;

    *handle = open(SPI_BUS, O_RDWR | O_NOCTTY);
    if(*handle < 0) goto error;
    if(ioctl(*handle, SPI_IOC_WR_MODE, &spi_mode) < 0) goto error;
    if(ioctl(*handle, SPI_IOC_RD_MODE, &spi_mode) < 0) goto error;
    if(ioctl(*handle, SPI_IOC_WR_BITS_PER_WORD, &spi_bitsPerWord) < 0) goto error;
    if(ioctl(*handle, SPI_IOC_RD_BITS_PER_WORD, &spi_bitsPerWord) < 0) goto error;
    if(ioctl(*handle, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) goto error;

    return 0;

error:
    closeGpio();
    if (*handle >= 0) close(*handle);
    *handle = -1;
    return -1;
}

void tml_close(int handle)
{
    closeGpio();
    if(handle >= 0) close(handle);
}

void tml_reset(int handle)
{
    (void)handle;
    if(gEnableLine.request) {
        gpiod_line_request_set_value(gEnableLine.request, gEnableLine.offset,
                                     GPIOD_LINE_VALUE_INACTIVE);
    }
    usleep(10 * 1000);
    if(gEnableLine.request) {
        gpiod_line_request_set_value(gEnableLine.request, gEnableLine.offset,
                                     GPIOD_LINE_VALUE_ACTIVE);
    }
    usleep(10 * 1000);
}

int tml_send(int handle, char *pBuff, int buffLen)
{
    struct spi_ioc_transfer spi;
    char tx_buf[257];
    char rx_buf[257] = {0};
    int ret;
    memset(&spi, 0x0, sizeof(spi));
    tx_buf[0] = 0x7F;
    memcpy(&tx_buf[1], pBuff, buffLen);
    spi.tx_buf = (unsigned long)tx_buf;
    spi.rx_buf = (unsigned long)rx_buf;
    spi.len = buffLen+1;
    spi.delay_usecs = 0;
    spi.speed_hz = SPI_SPEED;
    spi.bits_per_word = SPI_BITS;
    spi.tx_nbits = 0;
    spi.rx_nbits = 0;
    spi.cs_change = 1;
    ret = ioctl(handle, SPI_IOC_MESSAGE(1), &spi);
    if (rx_buf[0] != 0xFF) ret =0;
    else PRINT_BUF(">> ", pBuff, buffLen);
    usleep(10 * 1000);
    return ret;
}

int tml_receive(int handle, char *pBuff, int buffLen)
{
    int numRead = 0;
    struct timeval tv;
    fd_set rfds;
    int ret;

    //if(pnGetint())
    {
        FD_ZERO(&rfds);
        FD_SET(handle, &rfds);
        tv.tv_sec = 2;
        tv.tv_usec = 1;

        // ret = select(handle+1, &rfds, NULL, NULL, &tv);
        // if(ret <= 0) return 0;

        ret = SpiRead(handle, pBuff, 3);
        if (ret <= 0) return 0;
        numRead = 3;
        if(pBuff[2] + 3 > buffLen) return 0;

        ret = SpiRead(handle, &pBuff[3], pBuff[2]);
        if (ret <= 0) return 0;
        numRead += ret;

        PRINT_BUF("<< ", pBuff, numRead);
    }

    return numRead;
}

int tml_transceive(int handle, char *pTx, int TxLen, char *pRx, int RxLen)
{
    int NbBytes = 0;
    if(tml_send(handle, pTx, TxLen) == 0) {
	if(tml_send(handle, pTx, TxLen) == 0) return 0;
    }
    while(NbBytes==0) {usleep(10000); NbBytes = tml_receive(handle, pRx, RxLen);}
    return NbBytes;
}












