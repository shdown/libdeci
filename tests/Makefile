SOURCES := driver.c ../deci.c
HEADERS := ../deci.h
CFLAGS := -std=c99 -Wall -Wextra -Og -g3

all: driver_native driver_32

driver_native: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $@

driver_32: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -DDECI_WE_ARE_64_BIT=0 $(SOURCES) -o $@

clean:
	$(RM) driver_native driver_32

.PHONY: all clean
