BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`
.PHONY: all
all: $(BINS)
wfs:
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs:
	$(CC) $(CFLAGS) -o mkfs mkfs.c
.PHONY: clean
clean:
	rm -rf $(BINS)
t:
	./create_disk.sh
	./mkfs -d disk.img -i 65 -b 200
	rm -rf mnt/
	mkdir mnt
	./wfs disk.img -f -s mnt
te:
	fusermount -u mnt
