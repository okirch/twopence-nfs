
CFLAGS	= -Wall -O2
APPS	= nfs lock-close-open lockbench

all: $(APPS)

obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

%: obj/%.o
	$(CC) -o $@ $< -lpthread

clean:
	rm -rf obj $(APPS)
