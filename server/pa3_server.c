#include <arpa/inet.h>
#include <errno.h>
#include <helper.h>
#include <netinet/in.h>
#include <pa3_error.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "helper.h"

bool sigint_received = false;

ssize_t sigint_safe_write(int32_t fd, void* buf, size_t count) {
  ssize_t n_written;
  do {
    n_written = write(fd, buf, count);
  } while (n_written < 0 && errno == EINTR);
  return n_written;
}

ssize_t sigint_safe_read(int32_t fd, void* buf, size_t count) {
  ssize_t n_read;
  do {
    n_read = read(fd, buf, count);
  } while (n_read < 0 && errno == EINTR);
  return n_read;
}

// Use pthread_mutex_lock when accessing 'PollSet'
void add_to_pollset(PollSet* poll_set,
                    int32_t notification_fd,
                    int32_t connfd) {

  //printf("[add_to_pollset] connfd = %d added to thrasdfaead %d\n", connfd, poll_set->set[0].fd);
  fflush(stdout);
  //printf("DFDSFS"); fflush(stdout);
  // ???
  pthread_mutex_lock(&poll_set->mutex);  // 동기화
  //printf("[DEBUG] entering add_to_pollset\n"); fflush(stdout);
  //printf("[DEBUG] poll_set->set = %p\n", (void*)poll_set->set); fflush(stdout);

	// 공간 부족
  if (poll_set->size >= CLIENTS_PER_THREAD) {
    pthread_mutex_unlock(&poll_set->mutex);
    return;
  }

  poll_set->set[poll_set->size].fd = connfd;
  poll_set->set[poll_set->size].events = POLLIN;
  poll_set->size++;

  pthread_mutex_unlock(&poll_set->mutex);

  notify_pollset(notification_fd);
}

// This function is called within thread_func.
// Assuming you have already obtained the lock in thread_func, you do not need
// to lock the mutex here.
void remove_from_pollset(ThreadData* data, size_t* i_ptr) {
  // ???
  PollSet* poll_set = data->poll_set;

  pthread_mutex_lock(&poll_set->mutex);
  size_t i = *i_ptr;
  size_t last = poll_set->size - 1;

  close(poll_set->set[i].fd);  // 소켓 닫기

  if (i != last) {
    poll_set->set[i] = poll_set->set[last];  // 마지막 원소를 덮어쓰기
    *i_ptr = i - 1;  // 다음 루프에서 i++ 되기 때문에 조정
  }

  poll_set->size--;  // 개수 감소
  pthread_mutex_unlock(&poll_set->mutex);
}

// You have to poll the poll_set. When there is a file that is ready to be read,
// you have to lock the poll set to prevent cases where the poll set gets
// updated. You have to remember to unlock the poll set after you are done
// reading from it, including cases where you have to exit due to errors, or
// else your program might not be able to terminate without calling (p)kill.
void* thread_func(void* arg) {
  ThreadData* data = (ThreadData*)arg;
  PollSet* poll_set = data->poll_set;

  while (!sigint_received) {
    //printf("loop start: thread %zu\n", data->thread_index); fflush(stdout);
    // ???
    // pthread_mutex_lock(&poll_set->mutex);
    //printf("Thread %zu running. poll_set->size = %zu\n", data->thread_index, poll_set->size); fflush(stdout);

    int n_ready = poll(poll_set->set, poll_set->size, -1);
    if (n_ready < 0) {
      // printf("n_ready < 0\n"); fflush(stdout);
      // pthread_mutex_unlock(&poll_set->mutex);
      if (errno == EINTR) continue;
      perror("poll");
      break;
    }
    // pthread_mutex_unlock(&poll_set->mutex);
    // printf("Thread %zu: %d file descriptors ready\n", data->thread_index, n_ready); fflush(stdout);
    
    for (size_t i = 0; i < poll_set->size && n_ready > 0; i++) {
      //printf("Thread %zu: checking pollfd[%zu]\n", data->thread_index, i); fflush(stdout);
      struct pollfd* pollfd = &poll_set->set[i];

      if (pollfd->revents & POLLIN) {
        // pipe에서 깨운 경우 종료 시그널이면 처리, 아니면 무시
        //printf("pollfd->revents & POLLIN\n"); fflush(stdout);
        if (pollfd->fd == data->pipe_out_fd) {
          char dummy;
          read(pollfd->fd, &dummy, 1);
          if (sigint_received) {
            // pthread_mutex_unlock(&poll_set->mutex);
            pthread_exit(NULL);  // 쓰레드 종료
          }
          // pthread_mutex_unlock(&poll_set->mutex);
          continue;
        }

        // pthread_mutex_lock(&poll_set->mutex);

        Request request;
        default_request(&request);

        // Request 읽기 (TLV 방식)
        //printf("Read request - username_length, data_size, action\n"); fflush(stdout);
        if (sigint_safe_read(pollfd->fd, &request.username_length, sizeof(uint64_t)) <= 0) {
          remove_from_pollset(data, &i);
          i--;
          continue;
        }
        //printf("Read request - username_length, data_size, action\n"); fflush(stdout);
        if (sigint_safe_read(pollfd->fd, &request.data_size, sizeof(uint64_t)) <= 0) {
          remove_from_pollset(data, &i);
          i--;
          continue;
        }
        //printf("Read request - username_length, data_size, action\n"); fflush(stdout);
        if (sigint_safe_read(pollfd->fd, &request.action, sizeof(int32_t)) <= 0) {
          remove_from_pollset(data, &i);
          i--;
          continue;
        }
        if (request.username_length > 0) {
          request.username = malloc(request.username_length + 1);
          if (!request.username ||
              sigint_safe_read(pollfd->fd, request.username, request.username_length) <= 0) {
            remove_from_pollset(data, &i);
            i--;
            continue;
          }
          request.username[request.username_length] = '\0';
        }
        if (request.data_size > 0) {
          request.data = malloc(request.data_size + 1);
          if (!request.data ||
              sigint_safe_read(pollfd->fd, request.data, request.data_size) <= 0) {
            remove_from_pollset(data, &i);
            i--;
            continue;
          }
          request.data[request.data_size] = '\0';
        }

        // 요청 처리 및 응답
        Response response;
        handle_request(&request, &response, data->users, data->seats);

        // Response 전송
        if (sigint_safe_write(pollfd->fd, &response.data_size, sizeof(uint64_t)) <= 0 ||
            sigint_safe_write(pollfd->fd, &response.code, sizeof(int32_t)) <= 0 ||
            (response.data_size > 0 &&
            sigint_safe_write(pollfd->fd, response.data, response.data_size) <= 0)) {
          remove_from_pollset(data, &i);
          i--;
          continue;
        }

        free_request(&request);
        free_response(&response);

        n_ready--;
      }
      // pthread_mutex_unlock(&poll_set->mutex);
    } // for
  } // while
  pthread_exit(nullptr);
}

int main(int argc, char* argv[]) {
  setup_sigint_handler();

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    return 1;
  }

  int listenfd;
  struct sockaddr_in saddr, caddr;

  Users users;
  setup_users(&users);

  Seat* seats = default_seats();
  int32_t n_cores = get_num_cores();

  pthread_t* tid_arr = malloc(sizeof(pthread_t) * n_cores);
  ThreadData* data_arr = malloc(sizeof(ThreadData) * n_cores);
  int32_t (*pipe_fds)[2] = malloc(sizeof(int32_t[2]) * n_cores);

  for (int i = 0; i < n_cores; i++) {
    if (pipe(pipe_fds[i]) < 0) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }

    data_arr[i].thread_index = i;
    data_arr[i].pipe_out_fd = pipe_fds[i][0];
    data_arr[i].poll_set = create_poll_set(pipe_fds[i][0]);
    data_arr[i].users = &users;
    data_arr[i].seats = seats;
    //printf("Thread %d created with pipe fd %d\n", i, pipe_fds[i][0]); fflush(stdout);
    pthread_create(&tid_arr[i], nullptr, thread_func, &data_arr[i]);
  }

  listenfd = socket(AF_INET, SOCK_STREAM, 0);

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  saddr.sin_port = htons(strtoull(argv[1], nullptr, 10));

  bind(listenfd, (struct sockaddr*)&saddr, sizeof(saddr));
  listen(listenfd, 10);
  // if (bind(listenfd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
  //   perror("bind");
  //   exit(EXIT_FAILURE);
  // } else {
  //   printf("Successfully bound to port %s\n", argv[1]);
  // }
  // if (listen(listenfd, 10) == 0) {
  //   printf("Server is listening on port %s\n", argv[1]);
  // } else {
  //   perror("listen");
  //   exit(EXIT_FAILURE);
  // }

  struct pollfd main_thread_poll_set[2];
  memset(main_thread_poll_set, 0, sizeof(main_thread_poll_set));
  main_thread_poll_set[0].fd = STDIN_FILENO;
  main_thread_poll_set[0].events = POLLIN;
  main_thread_poll_set[1].fd = listenfd;
  main_thread_poll_set[1].events = POLLIN;

  while (!sigint_received) {
    if (poll(main_thread_poll_set, 2, -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("poll");
      exit(EXIT_FAILURE);
    }

    if (main_thread_poll_set[0].revents & POLLIN) {
      if (check_stdin_for_termination() == true) {
        kill(getpid(), SIGINT);
        for (int i = 0; i < n_cores; ++i) {
          write(pipe_fds[i][1], " ", 1);  // 깨우기용 dummy
        }
        continue;
      }
    } else if (main_thread_poll_set[1].revents & POLLIN) {
      uint32_t caddrlen = sizeof(caddr);
      int connfd = accept(listenfd, (struct sockaddr*)&caddr, &caddrlen);
      if (connfd < 0) {
        if (errno == EINTR) {
          continue;
        }
        puts("accept() failed");
        exit(EXIT_FAILURE);
      }

      printf("Accepted connection from client\n");
      ssize_t pollset_i;
      do {
        pollset_i = find_suitable_pollset(data_arr, n_cores);
        //printf("pollset_i: %zd\n", pollset_i);
      } while (pollset_i == -1);
      //printf("Adding connfd %d to pollset %zd\n", connfd, pollset_i);
      add_to_pollset(data_arr[pollset_i].poll_set, pipe_fds[pollset_i][1], connfd);
      //printf("after add_to_pollset, pollset size: %ld\n", data_arr[pollset_i].poll_set->size);
    }
  }

  return terminate_after_cleanup(pipe_fds, tid_arr, data_arr, n_cores, listenfd,
                                 &users, seats);
}