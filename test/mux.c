#include "test.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

char buf[0x400000];

#define NMUX 8
#define CHUNKSIZE 0x4000

int main() {
	int fd[NMUX];
	int i;
	for (i = 0; i < NMUX; i++) {
		fd[i] = open("/dev/crc0", O_RDWR);
		if (fd[i] < 0) {
			perror("open");
			return 1;
		}
		if (crcdev_ioctl_set_params(fd[i], 0xedb88320, 0xffffffff)) {
			perror("set_params");
			return 1;
		}
	}
	gen(buf, sizeof buf);
	int pos;
	for (pos = 0; pos < sizeof buf; pos += CHUNKSIZE) {
		for (i = 0; i < NMUX; i++) {
			if (write(fd[i], buf + pos, CHUNKSIZE) != CHUNKSIZE) {
				perror("write");
				return 1;
			}
		}
	}
	for (i = 0; i < NMUX; i++) {
		uint32_t sum;
		if (crcdev_ioctl_get_result(fd[i], &sum)) {
			perror("get_result");
			return 1;
		}
		sum ^= 0xffffffff;
		printf("%08x\n", sum);
	}
	return 0;
}
