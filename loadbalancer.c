#include <err.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdbool.h>

char healthCheckRequest[200];
char *internalServerError = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";

pthread_cond_t freeWorker = PTHREAD_COND_INITIALIZER;
pthread_cond_t workToDo = PTHREAD_COND_INITIALIZER;
pthread_cond_t confirm = PTHREAD_COND_INITIALIZER;
pthread_cond_t healthStart = PTHREAD_COND_INITIALIZER;
pthread_mutex_t healthMutex;

int activeConnections = 0;
int requestTotalAll = 0;
int toSkip = 0;

typedef struct server_info_t
{
  int port;
  bool alive;
  int total_requests;
  int error_requests;

}ServerInfo;

typedef struct servers_t
{
  int num_servers;
  ServerInfo * servers;
  pthread_mutex_t mut;
  int acceptfd;
}Servers;

Servers servers;

int client_connect(uint16_t connectport)
{
  int connfd;
  struct sockaddr_in servaddr;
  connfd = socket(AF_INET, SOCK_STREAM, 0);

  if (connfd < 0)
    return -1;

  memset(&servaddr, 0, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(connectport);
  inet_pton(AF_INET, "127.0.0.1", &(servaddr.sin_addr));

  if (connect(connfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
    return -1;
  return connfd;
}

void *do_periodic_healthcheck()
{
  while (true)
  {
    requestTotalAll = 0;
    char healthResponse[200];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    pthread_mutex_lock(&healthMutex);

    if (toSkip == 0)
    {
      pthread_cond_timedwait(&healthStart, &healthMutex, &ts);
    }

    toSkip = 0;

    for (int i = 1; i < servers.num_servers; i++)
    {
      uint16_t connectport = servers.servers[i].port;
      int tempSock = client_connect(connectport);

      if (tempSock == -1)
      {
        servers.servers[i].alive = false;
      }
      else
      {
        int n = send(tempSock, healthCheckRequest, strlen(healthCheckRequest), 0);

        if (n < 0)
        {
          servers.servers[i].alive = false;
        }
        else if (n == 0)
        {
          servers.servers[i].alive = false;
        }

        n = recv(tempSock, healthResponse, 200, 0);

        if (n < 0)
        {
          servers.servers[i].alive = false;
        }
        else if (n == 0)
        {
          servers.servers[i].alive = false;
        }
        else
        {
          healthResponse[n] = '\0';
          int responseNumber = -1;
          int read = sscanf(healthResponse, "HTTP/1.1 %d", &responseNumber);

          if (read < 1 || responseNumber != 200)
          {
            servers.servers[i].alive = false;
          }
          else
          {
            char *sentData;
            if (n < 40)
            {
              int m = recv(tempSock, healthResponse, 200, 0);

              if (m < 0)
              {
                servers.servers[i].alive = false;
              }
              else if (m == 0)
              {
                servers.servers[i].alive = false;
              }
              else
              {
                healthResponse[m] = '\0';
                sentData = strstr(healthResponse, "");
              }
            }
            else
            {
              sentData = strstr(healthResponse, "\r\n\r\n");
              sentData = sentData + 4;
            }

            int numRequests = -1;
            int numErrors = -1;
            read = sscanf(sentData, "%d\n%d", &numErrors, &numRequests);

            if (read < 2 || numRequests == -1 || numErrors == -1)
            {
              servers.servers[i].alive = false;
            }
            else
            {
              servers.servers[i].total_requests = numRequests;
              servers.servers[i].error_requests = numErrors;
              servers.servers[i].alive = true;
            }
          }
        }
      }
    }
    pthread_mutex_unlock(&healthMutex);
  }
}

int determine_best_server()
{
  int smallestRequests = -1;
  int errorCount = -1;
  int smallestId = -1;
  for (int i = 1; i < servers.num_servers; i++)
  {
    if (servers.servers[i].alive)
    {
      if (smallestId == -1)
      {
        smallestId = i;
        smallestRequests = servers.servers[i].total_requests;
        errorCount = servers.servers[i].error_requests;
      }
      else
      {
        if ((smallestRequests == servers.servers[i].total_requests && errorCount < servers.servers[i].error_requests) || smallestRequests > servers.servers[i].total_requests)
        {
          smallestId = i;
          smallestRequests = servers.servers[i].total_requests;
          errorCount = servers.servers[i].error_requests;
        }
      }
    }
  }

  return smallestId;
}

int init_servers(int argc, char **argv, int start_of_ports, pthread_t someThread)
{
  servers.num_servers = argc - start_of_ports;

  if (servers.num_servers <= 0)
  {
    return -1;
  }

  servers.servers = malloc(servers.num_servers* sizeof(ServerInfo));
  pthread_mutex_init(&servers.mut, NULL);
  servers.acceptfd = -1;
  pthread_create(&someThread, NULL, do_periodic_healthcheck, NULL);

  for (int i = start_of_ports; i < argc; i++)
  {
    int tempI = i - start_of_ports;
    servers.servers[tempI].port = atoi(argv[i]);

    if (servers.servers[tempI].port == 0)
    {
      return -1;
    }

    servers.servers[tempI].alive = false;
    servers.servers[tempI].total_requests = 0;
    servers.servers[tempI].error_requests = 0;
  }

  sprintf(healthCheckRequest, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", servers.servers[0].port);
  toSkip = 1;
  pthread_cond_signal(&healthStart);

  return 0;
}

int server_listen(int port)
{
  int listenfd;
  int enable = 1;
  struct sockaddr_in servaddr;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    return -1;
  memset(&servaddr, 0, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    return -1;
  if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof servaddr) < 0)
    return -1;
  if (listen(listenfd, 500) < 0)
    return -1;
  return listenfd;
}

int bridge_connections(int fromfd, int tofd)
{
  char recvline[4096];
  int n = recv(fromfd, recvline, 4096, 0);
  if (n < 0)
  {
    return -1;
  }
  else if (n == 0)
  {
    return 0;
  }
  recvline[n] = '\0';

  n = send(tofd, recvline, n, 0);
  if (n < 0)
  {
    return -1;
  }
  else if (n == 0)
  {
    return 0;
  }
  return n;
}

void bridge_loop(int sockfd1, int sockfd2)
{
  fd_set set;
  struct timeval timeout;

  int fromfd, tofd;
  while (1)
  {
    FD_ZERO(&set);
    FD_SET(sockfd1, &set);
    FD_SET(sockfd2, &set);

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout))
    {
      case -1:
        return;
      case 0:
        continue;
      default:
        if (FD_ISSET(sockfd1, &set))
        {
          fromfd = sockfd1;
          tofd = sockfd2;
        }
        else if (FD_ISSET(sockfd2, &set))
        {
          fromfd = sockfd2;
          tofd = sockfd1;
        }
        else
        {
          return;
        }
    }
    if (bridge_connections(fromfd, tofd) <= 0)
      return;
  }
}

void *worker()
{
  while (true)
  {
    pthread_mutex_lock(&servers.mut);

    if (activeConnections > 0)
    {
      activeConnections--;
    }

    pthread_cond_signal(&freeWorker);
    pthread_cond_wait(&workToDo, &servers.mut);

    int acceptfd = servers.acceptfd;
    servers.acceptfd = -1;
    activeConnections++;

    pthread_cond_signal(&confirm);
    pthread_mutex_unlock(&servers.mut);
    int bestServerId = determine_best_server();

    if (bestServerId == -1)
    {
      send(acceptfd, internalServerError, strlen(internalServerError), 0);
    }
    else
    {
      uint16_t connectport = servers.servers[bestServerId].port;
      int connfd = -1;

      if ((connfd = client_connect(connectport)) < 0)
      {
        servers.servers[bestServerId].alive = false;
        send(acceptfd, internalServerError, strlen(internalServerError), 0);
      }
      else
      {
        bridge_loop(acceptfd, connfd);
      }
    }
  }
}

int main(int argc, char **argv)
{
  int listenfd, acceptfd;
  uint16_t listenport;
  pthread_mutex_init(&healthMutex, NULL);

  if (argc < 3)
  {
    return 1;
  }

  int parallelConnections = 4;
  int requestPerCheck = 5;
  requestTotalAll = 0;

  int opt;
  int portStart = 1;
  while ((opt = getopt(argc, argv, "N:R:")) != -1)
  {
    switch (opt)
    {
      case 'R':
        requestPerCheck = atoi(optarg);
        portStart += 2;
        break;
      case 'N':
        parallelConnections = atoi(optarg);
        portStart += 2;
        break;
    }
  }
  pthread_t thread[parallelConnections + 1];

  int ret = init_servers(argc, argv, portStart, thread[parallelConnections]);

  if (ret == -1)
  {
    return -1;
  }

  listenport = servers.servers[0].port;

  if ((listenfd = server_listen(listenport)) < 0)
  {
    err(1, "failed listening");
  }

  for (int i = 0; i < parallelConnections; i++)
  {
    pthread_create(&thread[i], NULL, worker, NULL);
  }

  while (true)
  {
    if (requestTotalAll % requestPerCheck == 0 && requestTotalAll > 0)
    {
      pthread_cond_signal(&healthStart);
    }

    if ((acceptfd = accept(listenfd, NULL, NULL)) < 0)
    {
      err(1, "failed accepting");
    }

    pthread_mutex_lock(&servers.mut);
    servers.acceptfd = acceptfd;

    if (activeConnections >= parallelConnections)
    {
      pthread_cond_wait(&freeWorker, &servers.mut);
    }

    pthread_cond_signal(&workToDo);
    pthread_cond_wait(&confirm, &servers.mut);
    requestTotalAll++;
    pthread_mutex_unlock(&servers.mut);
  }
}