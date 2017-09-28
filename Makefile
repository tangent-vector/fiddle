#

CFLAGS 		 := -g
LDFLAGS		 :=

TARGET := skub
OUTPUTDIR := bin/
#

.SUFFIXES : .c $(OBJSUFFIX)

.PHONY : clean mkdirs skub


SOURCES := skub.c
HEADERS :=

all: mkdirs skub

skub: $(OUTPUTDIR)/$(TARGET)

$(OUTPUTDIR)$(TARGET): $(OUTPUTDIR) $(SOURCES) $(HEADERS)
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $(SOURCES)

mkdirs: $(OUTPUTDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

clean:
	rm -rf $(OUTPUTDIR)/$(TARGET)
