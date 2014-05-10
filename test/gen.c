#include "test.h"
#include <stdlib.h>

void gen(char *buf, size_t len) {
	unsigned short state[3];
	state[0] = 0x1234;
	state[1] = 0x5678;
	state[2] = 0x9abc;
	int i;
	for (i = 0; i < len; i++) {
		buf[i] = jrand48(state);
	}
}
