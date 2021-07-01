all: loadbalancer

loadbalancer: loadbalancer.o
	gcc -g -Wall -Wextra -Wpedantic -Wshadow -pthread -o loadbalancer loadbalancer.o

loadbalancer.o: loadbalancer.c
	gcc -g -Wall -Wextra -Wpedantic -Wshadow -pthread -c loadbalancer.c

clean:
	rm loadbalancer.o loadbalancer