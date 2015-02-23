
bindir	= /usr/bin
testdir	= /usr/share/testbus/suites

CFLAGS	= -Wall -g
APPS	= nfs lock-close-open lockbench
SCRIPTS	= testbus/nfs \
	  testbus/nfs.init \
	  testbus/nfs.coherence \
	  testbus/nfs.export \
	  testbus/nfs.fileops \
	  testbus/nfs.mount \
	  testbus/nfs.functions

CONFIG	= testbus/nfs.conf

all: $(APPS)

install: $(APPS)
	install -m 755 -d $(DESTDIR)$(bindir)
	install -m 555 $(APPS) $(DESTDIR)$(bindir)
	install -m 755 -d $(DESTDIR)$(testdir)
	install -m 555 twopence/run $(DESTDIR)$(testdir)

obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

%: obj/%.o
	$(CC) -o $@ $< -lpthread

clean:
	rm -rf obj $(APPS)
