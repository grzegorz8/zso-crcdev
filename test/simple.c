#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "test.h"

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
	char buf[3] = "abc";
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
