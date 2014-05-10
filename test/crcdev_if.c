#include "crcdev_ioctl.h"
#include "test.h"
#include <sys/ioctl.h>

int crcdev_ioctl_set_params(int fd, uint32_t poly, uint32_t sum) {
	struct crcdev_ioctl_set_params arg = { poly, sum };
	return ioctl(fd, CRCDEV_IOCTL_SET_PARAMS, &arg);
}

int crcdev_ioctl_get_result(int fd, uint32_t *sum) {
	struct crcdev_ioctl_get_result arg;
	int res = ioctl(fd, CRCDEV_IOCTL_GET_RESULT, &arg);
	if (res < 0)
		return res;
	*sum = arg.sum;
	return res;
}
