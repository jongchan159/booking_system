#include "helper.h"
#include <ctype.h>
#include <signal.h>
#include <stdlib.h>

void sigint_handler(int32_t signum) {
  if (signum == SIGINT)
    sigint_received = true;
}

void setup_sigint_handler() {
  struct sigaction action = {.sa_handler = sigint_handler, .sa_flags = 0};
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
}

void default_request(Request* request) {
  request->username = nullptr;
  request->username_length = 0;
  request->data = nullptr;
  request->data_size = 0;
  request->action = ACTION_INVALID;
}

void free_request(Request* request) {
  if (request->username != nullptr) {
    free(request->username);
    request->username = nullptr;
  }
  if (request->data != nullptr) {
    free(request->data);
    request->data = nullptr;
  }
}

void free_response(Response* response) {
  if (response->data != nullptr) {
    free(response->data);
    response->data = nullptr;
  }
}
