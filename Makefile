TARGET=normproxy
SRC=normproxy.c
GCC=g++
# change these directories to point to your local copy of NORM and Protolib
NORMDIR=$(HOME)/Downloads/norm
PROTOLIBDIR=$(HOME)/Downloads/protolib

default:
	$(GCC) -Wall -lresolv -pthread -I$(NORMDIR)/include -I$(PROTOLIBDIR)/include $(SRC) $(NORMDIR)/lib/libnorm.a $(PROTOLIBDIR)/lib/libprotokit.a -o $(TARGET)

clean:
	rm -f $(TARGET) *~
