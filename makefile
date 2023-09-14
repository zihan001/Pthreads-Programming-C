a4w23: a4w23.o
	gcc -o a4w23 a4w23.o -lpthread

a4w23.o: a4w23.c
	gcc -c a4w23.c -o a4w23.o

make clean:
	rm *.o

tar:
	tar -cvf Hossain-a4.tar a4w23.c input.txt CMPUT\ 379\ Assignment\ 4\ Report.pdf makefile