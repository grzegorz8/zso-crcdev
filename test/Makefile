PROGS = simple long thread thread1 mux rmux
EXTRA_SRC = crcdev_if.c gen.c
CFLAGS = -Wall

all: $(PROGS)

%: %.c $(EXTRA_SRC)
	gcc -pthread $(CFLAGS) $< $(EXTRA_SRC) -o $@

clean:
	rm -rf $(PROGS)
