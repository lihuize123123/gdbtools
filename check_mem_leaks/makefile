all:
	gcc -g -shared -fPIC -ldl -lpthread -DMEM_TRACE_LOG=0 libapicap.c -o libapicap.so

clean:
	rm -rf libapicap.so

clean_log:
	rm -rf mem_ops_*.log
