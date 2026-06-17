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
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <tml.h>
#include <stdint.h>
#include <stdlib.h>

#define TP() printf("%s:%s:%d\n", __FILE__, __func__, __LINE__)

int tml_open(int *handle)
{
    *handle = open((char const *)"/dev/nxpnfc", O_RDWR);
    if (*handle < 0) {
        *handle = open((char const *)"/dev/pn544", O_RDWR);
        if (*handle < 0) return -1;
    }
    return 0;
}

void tml_close(int handle)
{
    if(handle) close(handle);
}

void tml_reset(int handle)
{
    ioctl(handle, _IOW(0xE9, 0x01, uint32_t), 0);
    usleep(10 * 1000);
    ioctl(handle, _IOW(0xE9, 0x01, uint32_t), 1);
    usleep(10 * 1000);
}

int tml_send(int handle, char *pBuff, int buffLen)
{
    TP();
    int ret = write(handle, pBuff, buffLen);
    printf("write: ret = %d, buffLen = %d\n", ret, buffLen);
    if(ret <= 0) {
        TP();
        /* retry to handle standby mode */
        ret = write(handle, pBuff, buffLen);
        printf("write: ret = %d, buffLen = %d\n", ret, buffLen);
    }
    
    PRINT_BUF(">> ", pBuff, ret);
    return ret;
}

int tml_receive(int handle, char *pBuff, int buffLen)
{
    int numRead;
    struct timeval tv;
    fd_set rfds;
    int ret;

    TP();

    FD_ZERO(&rfds);
    TP();
    FD_SET(handle, &rfds);
    TP();
    tv.tv_sec = 2;
    tv.tv_usec = 1;
    ret = select(handle+1, &rfds, NULL, NULL, &tv);
    TP();

    if(ret <= 0) {
        printf("select: ret = %d\n", ret);
        exit(ret);
    }

    TP();
    ret = read(handle, pBuff, 3);
    TP();
    if (ret <= 0){TP(); return 0;}
    numRead = 3;
    if(pBuff[2] + 3 > buffLen) {TP(); return 0;}
    TP();
    ret = read(handle, &pBuff[3], pBuff[2]);
    if (ret <= 0) {TP();return 0;}
    numRead += ret;

    PRINT_BUF("<< ", pBuff, numRead);

    return numRead;
}

int tml_transceive(int handle, char *pTx, int TxLen, char *pRx, int RxLen)
{
    TP();
    if(tml_send(handle, pTx, TxLen) == 0) {TP(); return 0;}
    TP();
    return tml_receive(handle, pRx, RxLen);
}
