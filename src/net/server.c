#include <asm-generic/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "net/router.h"
#include "net/server.h"
#include "log/log.h"

#define MAX_EVENTS 64
#define BACKLOG 1024

typedef struct {
  int epoll_fd;
  _Atomic int conn_count;
} Worker;

static int listen_fd = -1;
static int num_workers;
static Worker* workers;
static pthread_t* threads;
static volatile bool running = true;

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static Worker* least_busy_worker(void) {
  Worker* best = &workers[0];
  for (int i = 1; i < num_workers; i++) {
    if (workers[i].conn_count < best->conn_count) {
      best = &workers[i];
    }
  }

  return best;
}

static void handle_request(int fd) {
  char buf[4096];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);

  if (n <= 0) {
    close(fd);
    return;
  }

  buf[n] = '\0';

  router_dispatch(fd, buf, n);

  close(fd);
}

static void* worker_loop(void* arg) {
  Worker* worker = (Worker*)arg;
  struct epoll_event events[MAX_EVENTS];

  while (running) {
    int n = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 100);
    for (int i = 0; i < n; i++) {
      if (events[i].events & EPOLLIN) {
        int fd = events[i].data.fd;
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        handle_request(fd);
        worker->conn_count--;
      }
    }
  }

  return NULL;
}

static void shutdown_workers(int threads_started) {
  running = false;
  for (int i = 0; i < threads_started; i++) {
    pthread_join(threads[i], NULL);
    close(workers[i].epoll_fd);
  }
  free(workers);
  workers = NULL;
  free(threads);
  threads = NULL;
}

bool server_init(void) {
  num_workers = get_nprocs();
  log_info("starting %d worker threads", num_workers);

  workers = calloc(num_workers, sizeof(Worker));
  threads = malloc(num_workers * sizeof(pthread_t));
  if (!workers || !threads) {
    log_error("server_init: malloc failed");
    free(workers);
    free(threads);
    workers = NULL;
    threads = NULL;
    return false;
  }

  int threads_started = 0;
  for (int i = 0; i < num_workers; i++) {
    workers[i].epoll_fd = epoll_create1(0);
    if (workers[i].epoll_fd == -1) {
      log_error("epoll_create1 failed: %s", strerror(errno));
      shutdown_workers(threads_started);
      return false;
    }

    workers[i].conn_count = 0;
    if (pthread_create(&threads[i], NULL, worker_loop, &workers[i]) != 0) {
      log_error("pthread_create failed: %s", strerror(errno));
      close(workers[i].epoll_fd);
      workers[i].epoll_fd = -1;
      shutdown_workers(threads_started);
      return false;
    }
    threads_started++;
  }

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    log_error("socket failed: %s", strerror(errno));
    shutdown_workers(threads_started);
    return false;
  }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  set_nonblocking(listen_fd);

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = INADDR_ANY,
    .sin_port = htons(8080)
  };

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    log_error("bind failed: %s", strerror(errno));
    close(listen_fd);
    listen_fd = -1;
    shutdown_workers(threads_started);
    return false;
  }

  if (listen(listen_fd, BACKLOG) == -1) {
    log_error("listen failed: %s", strerror(errno));
    close(listen_fd);
    listen_fd = -1;
    shutdown_workers(threads_started);
    return false;
  }

  log_info("server listening on port 8080");
  return true;
}

void server_run(void) {
  while (running) {
    int conn_fd = accept(listen_fd, NULL, NULL);
    if (conn_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }

      log_error("accept failed: %s", strerror(errno));
      continue;
    }

    set_nonblocking(conn_fd);

    Worker* worker = least_busy_worker();
    worker->conn_count++;

    struct epoll_event ev = {
      .events = EPOLLIN | EPOLLET,
      .data.fd = conn_fd
    };
    epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev);
  }
}

void server_shutdown(void) {
  running = false;
  for (int i = 0; i < num_workers; i++) {
    pthread_join(threads[i], NULL);
    close(workers[i].epoll_fd);
  }

  close(listen_fd);
  free(workers);
  free(threads);
  log_info("server shut down");
}
