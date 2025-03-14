mntr: mntr.c thpool.c kstring.c
	gcc -static -o $@ $^  -Wno-discarded-qualifiers -lprocps -lpthread -lm

clean:
	rm -f mntr
