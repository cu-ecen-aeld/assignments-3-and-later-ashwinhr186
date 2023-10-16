LDFLAGS = -lpthread -pthread
CC ?= ${CROSS_COMPILE}gcc -Wall -Werror -g

all: aesdsocket
	${CC} aesdsocket.c -o aesdsocket ${LDFLAGS}

clean:
	rm -f aesdsocket