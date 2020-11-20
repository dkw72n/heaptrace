.PHONY: clean test

mtrace.so: mtrace.o mmtrace.o bt.o wrap.o
	g++ -fvisibility=hidden -shared -static-libstdc++ -static-libgcc wrap.o bt.o mtrace.o mmtrace.o -o mtrace.so -ldl -lm -pthread -Wl,--wrap=memcpy -Wl,--exclude-libs,libstdc++.a
	#g++ mmtrace.cpp mtrace.c -std=c++11 -ldl -shared -fPIC -g -o mtrace.so -lm

mtrace.o: mtrace.c mmtrace.h
	gcc mtrace.c -fvisibility=hidden -std=c11 -fPIC -g -c -o mtrace.o

bt.o: bt.cpp
	g++ bt.cpp -fvisibility=hidden -std=c++17 -fPIC -g -c -o bt.o

mmtrace.o: mmtrace.cpp mmtrace.h sampler.h
	g++ mmtrace.cpp -fvisibility=hidden -std=c++17 -fPIC -g -c -o mmtrace.o

wrap.o: wrap.c
	gcc wrap.c -fvisibility=hidden -g -fPIC -c -o wrap.o

clean:
	rm -f mtrace.so *.o

test: mtrace.so
	rm MallocMon -rf
	LD_PRELOAD=`pwd`/mtrace.so MTRACE_SAMPLE_INTERVAL=2 ./demo

