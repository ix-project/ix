CFLAGS=-Wall -g -MD -O3 -I../inc
LDFLAGS=-lrt

ix-stats-show: ix-stats-show.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f ix-stats-show *.o *.d

.PHONY: clean

-include *.d
