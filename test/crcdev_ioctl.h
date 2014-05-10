#ifndef CRCDEV_IOCTL_H
#define CRCDEV_IOCTL_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdint.h>
#endif

#include <linux/ioctl.h>

struct crcdev_ioctl_set_params {
	uint32_t poly;
	uint32_t sum;
};
#define CRCDEV_IOCTL_SET_PARAMS _IOW('C', 0x00, struct crcdev_ioctl_set_params)

struct crcdev_ioctl_get_result {
	uint32_t sum;
};
#define CRCDEV_IOCTL_GET_RESULT _IOR('C', 0x01, struct crcdev_ioctl_get_result)

#endif
