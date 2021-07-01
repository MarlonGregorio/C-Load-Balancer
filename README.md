# Load Balancer

Distributes connections to a set number of servers from a listen port. Will prioritize servers that have the least amount of errors. If servers go down it will avoid using those servers until they respond. The type of server that this load balancer supports is located in the C-Multi-Threaded-Server repository.

## Past Assignment
This load balancer was an assignment from my principles of computer systems design class when I was an undergrad at UCSC.

## Executing program

Running make should result in a binary file called loadbalancer. It needs to be run with at least 2 arguments beside the program name specifying a port to listen for clients and a server port to send the clients to. Multiple server ports can be used as well, as long as the listen port for the load balancer is the first of all the ports. Two optional arguments can be placed on top of everything. -N followed by a positive integer will result in that many parallel connections allowed to be served at once. -R followed by a positive number will force health checks to occur after R requests.
