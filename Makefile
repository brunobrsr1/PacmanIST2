.PHONY: all client server clean

all: client server

client:
	$(MAKE) -C client-base-with-Makefile-v3 client

server:
	$(MAKE) -C client-base-with-Makefile-v3 server

clean:
	$(MAKE) -C client-base-with-Makefile-v3 clean
