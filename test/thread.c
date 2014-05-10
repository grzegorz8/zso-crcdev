#include "test.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>

char buf[0x400000];

void *tmain(void *arg) {
	int fd = open("/dev/crc0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return buf;
	}
	if (crcdev_ioctl_set_params(fd, 0xedb88320, 0xffffffff)) {
		perror("set_params");
		return buf;
	}
	if (write(fd, buf, (sizeof buf)/4) != (sizeof buf)/4) {
		perror("write");
		return buf;
	}
    if (write(fd, buf + (sizeof buf)/4, (sizeof buf)/4) != (sizeof buf)/4) {
		perror("write");
		return buf;
	}
    if (write(fd, buf + 2*(sizeof buf)/4, (sizeof buf)/4) != (sizeof buf)/4) {
		perror("write");
		return buf;
	}
    if (write(fd, buf + 3*(sizeof buf)/4, (sizeof buf)/4) != (sizeof buf)/4) {
		perror("write");
		return buf;
	}
	uint32_t sum;
	if (crcdev_ioctl_get_result(fd, &sum)) {
		perror("get_result");
		return buf;
	}
	sum ^= 0xffffffff;
	printf("%08x\n", sum);
	return 0;
}

#define NTHREADS 8

int main() {
	gen(buf, sizeof buf);
	int i;
	pthread_t thr[NTHREADS];
	for (i = 0; i < NTHREADS; i++) {
		if (pthread_create(&thr[i], NULL, tmain, NULL)) {
			perror("pthread_create");
			return 1;
		}
	}
	for (i = 0; i < NTHREADS; i++) {
		void *res;
		if (pthread_join(thr[i], &res)) {
			perror("pthread_create");
			return 1;
		}
	}
	return 0;
}
