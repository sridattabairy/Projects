thread : mythread.h thread.c
	 gcc -c thread.c
	 ar rcs mythread.a thread.o 
	 rm thread.o 
