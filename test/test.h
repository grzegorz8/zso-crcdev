#include <stdint.h>
#include <stddef.h>

int crcdev_ioctl_set_params(int fd, uint32_t poly, uint32_t sum);
int crcdev_ioctl_get_result(int fd, uint32_t *sum);
void gen(char *buf, size_t len);
