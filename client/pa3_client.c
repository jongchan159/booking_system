#include <arpa/inet.h>
#include <ctype.h>
#include <editline/readline.h>
#include <helper.h>
#include <netinet/in.h>
#include <pa3_error.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "handle_response.h"
#include "helper.h"

#define CLEAR_SCREEN "\033[H\033[J"

const char* active_user = nullptr;
bool sigint_received = false;

int32_t get_socket(const char* ip, uint64_t port) {
  // socket() -> connect() -> return socket fd
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Connected to server %s:%lu\n", ip, port);
  // 서버에 연결 성공

  return sockfd;
}

void send_request(int32_t sockfd, Request* request) {
  // ???
  // 1. username_length
  if (write(sockfd, &request->username_length, sizeof(uint64_t)) <= 0) {
    perror("write username_length");
    return;
  }

  // 2. data_size
  if (write(sockfd, &request->data_size, sizeof(uint64_t)) <= 0) {
    perror("write data_size");
    return;
  }

  // 3. action
  int32_t action_value = (int32_t)request->action;
  if (write(sockfd, &action_value, sizeof(int32_t)) <= 0) {
    perror("write action");
    return;
  }

  // 4. username (username_length 바이트)
  if (request->username_length > 0 && request->username != NULL) {
    if (write(sockfd, request->username, request->username_length) <= 0) {
      perror("write username");
      return;
    }
  }

  // 5. data (data_size 바이트)
  if (request->data_size > 0 && request->data != NULL) {
    if (write(sockfd, request->data, request->data_size) <= 0) {
      perror("write data");
      return;
    }
  }
}

void receive_response(int32_t sockfd, Response* response) {
  memset(response, 0, sizeof(Response));

  // 1. data_size
  uint64_t data_size = 0;
  size_t total_read = 0;
  while (total_read < sizeof(uint64_t)) {
    ssize_t n = read(sockfd, ((char*)&data_size) + total_read, sizeof(uint64_t) - total_read);
    if (n <= 0) {
      perror("read data_size");
      return;
    }
    total_read += n;
  }
  response->data_size = data_size;

  // 2. code
  int32_t code = 0;
  total_read = 0;
  while (total_read < sizeof(int32_t)) {
    ssize_t n = read(sockfd, ((char*)&code) + total_read, sizeof(int32_t) - total_read);
    if (n <= 0) {
      perror("read code");
      return;
    }
    total_read += n;
  }
  response->code = code;

  // 3. data (optional)
  if (response->data_size > 0) {
    response->data = malloc(response->data_size);
    if (!response->data) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }

    total_read = 0;
    while (total_read < response->data_size) {
      ssize_t n = read(sockfd, ((char*)response->data) + total_read, response->data_size - total_read);
      if (n <= 0) {
        perror("read data");
        free(response->data);
        response->data = NULL;
        response->data_size = 0;
        return;
      }
      total_read += n;
    }
  } else {
    response->data = NULL;
  }
}

void terminate(int32_t sockfd, const char* active_user) {
  if (active_user != nullptr) {
    // TODO: send logout request and cleanup request, response
    Request request;
    default_request(&request);

    request.action = ACTION_LOGOUT;
    request.username = strdup(active_user);
    request.username_length = strlen(active_user);

    send_request(sockfd, &request);

    Response response;
    receive_response(sockfd, &response);

    handle_response(request.action, &request, &response, nullptr);

    free_request(&request);
    free_response(&response);
  }
}

int main(int argc, char* argv[]) {
  setup_sigint_handler();

  if (argc != 3) {
    fprintf(stderr, "usage: %s <IP address> <port>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int32_t sockfd = get_socket(argv[1], strtoull(argv[2], nullptr, 10));

  char* input = nullptr;
  while (((input = readline("")) != nullptr) && !sigint_received) {
    add_history(input);

    if (strncmp(input, "exit", 4) == 0 || strncmp(input, "quit", 4) == 0) {
      break;
    } else if (strncmp(input, "clear", 5) == 0) {
      printf(CLEAR_SCREEN);
      continue;
    }

    Request request;
    ParsingError parsing_error = parse_request(&request, input, &active_user);
    free_input(&input);

    if (parsing_error != PARSING_SUCCESS)
      continue;

    //printf("Sending request: action=%d, username=%s, data_size=%lu, data=%s\n", request.action, request.username, request.data_size, (char*)request.data);
    send_request(sockfd, &request);
    //printf("Request sent: action=%d, username=%s, data_size=%lu\n", request.action, request.username, request.data_size);

    Response response;
    receive_response(sockfd, &response);
    //printf("Receiving response: code=%d, data_size=%lu\n", response.code, response.data_size);
    handle_response(request.action, &request, &response, &active_user);
    //printf("Response received: code=%d, data_size=%lu\n", response.code, response.data_size);
    if (response.data != NULL) {
      //printf("Response data: %.*s\n", (int)response.data_size, response.data);
    } else {
      //printf("No response data.\n");
    }
    free_request(&request);
    free_response(&response);
  }

  free_input(&input);
  terminate(sockfd, active_user);
  close(sockfd);
  return 0;
}
