CC = gcc
CFLAGS = -std=c99 -O3 -Wall -Wpedantic

all: image_tagger

image_tagger:  image_tagger.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) image_tagger
