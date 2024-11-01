SRCS=$(wildcard *.c)

OBJS=$(SRCS:.c=.o)

TARGS=$(SRCS:.c=)

all: $(TARGS)

# $(TARGS): $(SRCS)
# 	gcc -o $@ $@.c

# and finally, a clean line
clean: 
	rm -f $(OBJS) $(TARGS) *.out
