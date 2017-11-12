#

CFLAGS 		 := -g
LDFLAGS		 :=

TARGET := skub
OUTPUTDIR := bin/
#

.SUFFIXES : .c $(OBJSUFFIX)

.PHONY : clean mkdirs skub test


SOURCES := skub.c
HEADERS :=

SKUB := $(OUTPUTDIR)skub

all: mkdirs skub test

skub: $(SKUB)

$(OUTPUTDIR)$(TARGET): $(OUTPUTDIR) $(SOURCES) $(HEADERS)
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $(SOURCES)

mkdirs: $(OUTPUTDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

test: $(SKUB)
	$(SKUB) test.cpp


clean:
	rm -rf $(OUTPUTDIR)/$(TARGET)
