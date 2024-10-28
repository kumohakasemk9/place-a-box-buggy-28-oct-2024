CC=gcc
CFLAGS=-g3
LIBS=allegro-5 allegro_font-5 allegro_primitives-5
OBJS=main.o
OUT=game

$(OUT): $(OBJS)
	$(CC) `pkg-config --libs $(LIBS)` $^ -o $@

clean:
	rm $(OBJS) $(OUT)
