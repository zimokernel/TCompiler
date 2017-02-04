CFLAGS=-Wall -std=gnu99 -g

OBJS=lex.o string.o util.o

$(OBJS) unittest.o main.o: tcc.h

tcc: tcc.h main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ main.o $(OBJS)

unittest: tcc.h unittest.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ unittest.o $(OBJS)

#test: unittest
#    ./unittest
#    ./test.sh

clean:
	rm -f tcc unittest *.o tmp.*
