#ifndef ADCCHARDEV_H
#define ADCCHARDEV_H

#include <linux/ioctl.h>

#define MAGIC_NUM 100

//select channel of adc
#define SEL_CHANNEL _IOR(MAGIC_NUM, 1, int *)

//select alignment of data in register
#define SEL_ALIGNMENT _IOR(MAGIC_NUM, 2, char *)

#define SEL_CONV _IOR(MAGIC_NUM, 3, int *)

#define DEVICE_FILE_NAME "/dev/adc-dev"


#endif
