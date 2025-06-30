#include <helper.h>
#include <pa3_error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helper.h"

LoginErrorCode handle_login_request(const Request* request,
                                    Response* response,
                                    Users* users) {
	response->data = NULL;
	response->data_size = 0;
		
  // LOGIN_ERROR_NO_PASSWORD -> reuqest 데이터가 비어있음
  if (!request || !request->username || !request->data) {
	  response->code = LOGIN_ERROR_NO_PASSWORD;
	  return LOGIN_ERROR_NO_PASSWORD;
	}

  ssize_t user_idx = find_user(users, request->username);
  if (user_idx == -1) {
    // 유저 찾기 실패 -> 새로 등록
    // hash_password 호출 후 배열에 저장
    printf("User %s not found, creating new user.\n", request->username);
    char hashed_pw[HASHED_PASSWORD_SIZE];
    hash_password(request->data, hashed_pw);
    size_t new_idx = add_user(users, request->username, hashed_pw);
    users->array[new_idx].logged_in = true;

    response->code = LOGIN_ERROR_SUCCESS;
    return LOGIN_ERROR_SUCCESS;
  }

  User* user = &users->array[user_idx];
  // LOGIN_ERROR_ACTIVE_USER
  if (user->logged_in) {
	  response->code = LOGIN_ERROR_ACTIVE_USER;
    return LOGIN_ERROR_ACTIVE_USER;
  }

	// LOGIN_ERROR_ACTIVE_CLIENT
	// 모르겠어 걍 감점 ㄱㄱ
	
	// LOGIN_ERROR_INCORRECT_PASSWORD;
  if (!validate_password(request->data, user->hashed_password)) {
	  response->code = LOGIN_ERROR_INCORRECT_PASSWORD;
    return LOGIN_ERROR_INCORRECT_PASSWORD;
  }

	// SUCCESS -> All pass
  user->logged_in = true;

  response->code = LOGIN_ERROR_SUCCESS;
  return LOGIN_ERROR_SUCCESS;
}

BookErrorCode handle_book_request(const Request* request,
                                  Response* response,
                                  Users* users,
                                  Seat* seats) {
  // Use pthread_mutex_lock when accessing 'Seat'
	response->data = NULL;
	response->data_size = 0;
	
  // BOOK_ERROR_NO_DATA - request 데이터가 올바른지 확인
  if (!request || !request->data) {
	  response->code = BOOK_ERROR_NO_DATA;
	  return BOOK_ERROR_NO_DATA;
	}
  
  // BOOK_ERROR_USER_NOT_LOGGEN_IN
  ssize_t user_idx = find_user(users, request->username);
  if (user_idx == -1 || !users->array[user_idx].logged_in){
	  response->code = BOOK_ERROR_USER_NOT_LOGGED_IN;
    return BOOK_ERROR_USER_NOT_LOGGED_IN;
  }

	// 좌석 정의, BOOK_ERROR_SEAT_OUT_OF_RANGE
  int seat_id = atoi(request->data);
  if (seat_id < 0 || seat_id >= NUM_SEATS) {
	  response->code = BOOK_ERROR_SEAT_OUT_OF_RANGE;
		return BOOK_ERROR_SEAT_OUT_OF_RANGE;
	}

	// BOOK_ERROR_SEAT_UNAVAILABLE
  pthread_mutex_lock(&seats[seat_id].mutex);
  if (seats[seat_id].user_who_booked != NULL) { // 이미 예약상태
    pthread_mutex_unlock(&seats[seat_id].mutex);
    response->code = BOOK_ERROR_SEAT_UNAVAILABLE;
    return BOOK_ERROR_SEAT_UNAVAILABLE;
  }

	// SUCCESS - All Pass
  char* booked_user = malloc(strlen(request->username) + 1);
  if (!booked_user) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  strcpy(booked_user, request->username);
  seats[seat_id].user_who_booked = booked_user; // 예약한 유저 저장
  pthread_mutex_unlock(&seats[seat_id].mutex);

  response->code = BOOK_ERROR_SUCCESS;
  return BOOK_ERROR_SUCCESS;
}

ConfirmBookingErrorCode handle_confirm_booking_request(const Request* request,
                                                       Response* response,
                                                       Users* users,
                                                       Seat* seats) {
	response->data = NULL;
	response->data_size = 0;
	
	// BCONFIRM_BOOKING_ERROR_NO_DATA - request 데이터가 올바른지 확인
  if (!request || !request->data) {
		response->code = CONFIRM_BOOKING_ERROR_NO_DATA;
	  return CONFIRM_BOOKING_ERROR_NO_DATA;
	}
  
  // CONFIRM_BOOKING_ERROR_USER_NOT_LOGGED_IN
  ssize_t user_idx = find_user(users, request->username);
  if (user_idx == -1 || !users->array[user_idx].logged_in){
		response->code = CONFIRM_BOOKING_ERROR_USER_NOT_LOGGED_IN;
    return CONFIRM_BOOKING_ERROR_USER_NOT_LOGGED_IN;
  }
  
  // CONFIRM_BOOKING_ERROR_USER_NOT_LOGGED_IN
  if (strcmp(request->data, "available") != 0 && strcmp(request->data, "booked") != 0) {
	  response->code = CONFIRM_BOOKING_ERROR_INVALID_DATA;
    return CONFIRM_BOOKING_ERROR_INVALID_DATA;
  }

  // SUCCESS - All Pass
  // 주의 - 과제명세 대로 하면 불일치 -> seat_t* 배열로 전달할 것 (handle_response 참조
  // 배열 생성
  pa3_seat_t* seat_ids = malloc(sizeof(pa3_seat_t) * NUM_SEATS);
  if (!seat_ids) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  
  size_t count = 0;
  for (int i = 0; i < NUM_SEATS; ++i) {
    pthread_mutex_lock(&seats[i].mutex);

		// confirm-booking available
    if (strcmp(request->data, "available") == 0) {
      if (seats[i].user_who_booked == NULL) {
        seat_ids[count++] = i; // 예약 안 된 좌석
      }
    } 
    // confirm-booking booked
    else if (strcmp(request->data, "booked") == 0) {
      if (seats[i].user_who_booked &&
          strcmp(seats[i].user_who_booked, request->username) == 0) {
        seat_ids[count++] = i; // 해당 유저가 예약한 좌석
      }
    }

    pthread_mutex_unlock(&seats[i].mutex);
  }

  // 결과 설정
  if (count > 0) {
    response->data = malloc(sizeof(pa3_seat_t) * count);
    if (!response->data) {
      free(seat_ids);
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    memcpy(response->data, seat_ids, sizeof(pa3_seat_t) * count);
    response->data_size = sizeof(pa3_seat_t) * count;
  }
  response->code = CONFIRM_BOOKING_ERROR_SUCCESS;

  free(seat_ids);
  return CONFIRM_BOOKING_ERROR_SUCCESS;
}

CancelBookingErrorCode handle_cancel_booking_request(const Request* request,
                                                     Response* response,
                                                     Users* users,
                                                     Seat* seats) {
  // Use pthread_mutex_lock when accessing 'Seat'
	response->data = NULL;
	response->data_size = 0;
	
  // CANCEL_BOOKING_ERROR_NO_DATA;
  if (!request || !request->data) {
	  response->code = CANCEL_BOOKING_ERROR_NO_DATA;
	  return CANCEL_BOOKING_ERROR_NO_DATA;
	}

  // CANCEL_BOOKING_ERROR_USER_NOT_LOGGED_IN
  ssize_t user_idx = find_user(users, request->username);
  if (user_idx == -1 || !users->array[user_idx].logged_in) {
		response->code = CANCEL_BOOKING_ERROR_USER_NOT_LOGGED_IN;
    return CANCEL_BOOKING_ERROR_USER_NOT_LOGGED_IN;
  }
    
  // 좌석 정의 및 CANCEL_BOOKING_ERROR_SEAT_OUT_OF_RANGE 처리
  int seat_id = atoi(request->data);
  if (seat_id < 0 || seat_id >= NUM_SEATS) {
	  response->code = CANCEL_BOOKING_ERROR_SEAT_OUT_OF_RANGE;
    return CANCEL_BOOKING_ERROR_SEAT_OUT_OF_RANGE;
  }

	// CANCEL_BOOKING_ERROR_SEAT_NOT_BOOKED_BY_USER
  pthread_mutex_lock(&seats[seat_id].mutex);
  if (!seats[seat_id].user_who_booked ||
      strcmp(seats[seat_id].user_who_booked, request->username) != 0) {
    pthread_mutex_unlock(&seats[seat_id].mutex);
    response->code = CANCEL_BOOKING_ERROR_SEAT_NOT_BOOKED_BY_USER;
    return CANCEL_BOOKING_ERROR_SEAT_NOT_BOOKED_BY_USER;
  }

	// SUCCESS - All Pass
  free((char*)seats[seat_id].user_who_booked); // 좌석 공간 해제
  seats[seat_id].user_who_booked = NULL; // 좌석 배열 내용 초기화
  pthread_mutex_unlock(&seats[seat_id].mutex);

  response->code = CANCEL_BOOKING_ERROR_SUCCESS;
  return CANCEL_BOOKING_ERROR_SUCCESS;
}

LogoutErrorCode handle_logout_request(const Request* request,
                                      Response* response,
                                      Users* users) {
	response->data = NULL;
	response->data_size = 0;
	
	// USER_NOT_FOUND
  ssize_t user_idx = find_user(users, request->username);
  if (user_idx == -1) {
	  response->code = LOGOUT_ERROR_USER_NOT_FOUND;
    return LOGOUT_ERROR_USER_NOT_FOUND;
  }
  
  // USER_NOT_LOGGED_IN
	if(!users->array[user_idx].logged_in) {
	  response->code = LOGOUT_ERROR_USER_NOT_LOGGED_IN;
    return LOGOUT_ERROR_USER_NOT_LOGGED_IN;
  }

	// SUCCESS -> log out
  users->array[user_idx].logged_in = false;

  response->code = LOGOUT_ERROR_SUCCESS;
  return LOGOUT_ERROR_SUCCESS;
}

QueryErrorCode handle_query_request(const Request* request,
                                    Response* response,
                                    Seat* seats) {
  // Use pthread_mutex_lock when accessing 'Seat'
  // need struct Seat not pa3_seat_t (wrong spectification)
  // QUERY_ERROR_NO_DATA
  if (!request || !request->data) return QUERY_ERROR_NO_DATA;

	// 좌석 정의 및 QUERY_ERROR_SEAT_OUT_OF_RANGE
  int seat_id = atoi(request->data);
  if (seat_id < 0 || seat_id >= NUM_SEATS) return QUERY_ERROR_SEAT_OUT_OF_RANGE;

  response->data = malloc(sizeof(Seat)); // 1-len array
	if (!response->data) {
		perror("malloc");
		exit(EXIT_FAILURE);
  }

  pthread_mutex_lock(&seats[seat_id].mutex);
  memcpy(response->data, &seats[seat_id], sizeof(Seat));
  pthread_mutex_unlock(&seats[seat_id].mutex);
  
	response->data_size = sizeof(Seat);
  response->code = QUERY_ERROR_SUCCESS;
  
  return QUERY_ERROR_SUCCESS;
}

int32_t handle_request(const Request* request,
                       Response* response,
                       Users* users,
                       Seat* seats) {
  switch (request->action) {
    case ACTION_LOGIN:
      return handle_login_request(request, response, users);
    case ACTION_BOOK:
      return handle_book_request(request, response, users, seats);
    case ACTION_CONFIRM_BOOKING:
      return handle_confirm_booking_request(request, response, users, seats);
    case ACTION_CANCEL_BOOKING:
      return handle_cancel_booking_request(request, response, users, seats);
    case ACTION_LOGOUT:
      return handle_logout_request(request, response, users);
    case ACTION_QUERY:
      return handle_query_request(request, response, seats);
    case ACTION_TERMINATION:
    //
    default:
      return -1;
  }
}