CFLAG=-g
OBJECT=send.o

all:$(OBJECT)
	gcc $(OBJECT) -o send -lpthread
clean:
	rm -r $(OBJECT)
	rm -r send
run:$(OBJECT)
	./send
