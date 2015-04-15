
bindir	= /usr/bin

CFLAGS	= -Wall -g
APPS	= nfs lock-close-open lockbench

all: $(APPS)

install: $(APPS)
	install -m 755 -d $(DESTDIR)$(bindir)
	install -m 555 $(APPS) $(DESTDIR)$(bindir)
	/usr/lib/susetest/twopence-install nfs twopence/nodes twopence/run $(DESTDIR)

obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

%: obj/%.o
	$(CC) -o $@ $< -lpthread

clean:
	rm -rf obj $(APPS)
