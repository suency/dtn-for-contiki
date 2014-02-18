CONTIKI = ../..

CONTIKI_PROJECT = example-dtn

all: $(CONTIKI_PROJECT)

CONTIKI_SOURCEFILES += dtn.c

include $(CONTIKI)/Makefile.include

