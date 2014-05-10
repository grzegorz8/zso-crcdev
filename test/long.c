#include "test.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

char buf[0x400000];

int main() {
	int fd = open("/dev/crc0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (crcdev_ioctl_set_params(fd, 0xedb88320, 0xffffffff)) {
		perror("set_params");
		return 1;
	}
	gen(buf, sizeof buf);
	if (write(fd, buf, sizeof buf) != sizeof buf) {
		perror("write");
		return 1;
	}
	uint32_t sum;
	if (crcdev_ioctl_get_result(fd, &sum)) {
		perror("get_result");
		return 1;
	}
	sum ^= 0xffffffff;
	printf("%08x\n", sum);
	return 0;
}
