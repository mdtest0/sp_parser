sp_parser: main.o
	@gcc -g -o $@ $^ -ljson-c -levent

main.o: main.c
	@gcc -g -c -o $@ $<

clean:
	@rm -f *.o sp_parser 

.PHONY: sp_parser clean
