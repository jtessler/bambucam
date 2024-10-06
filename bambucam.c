#include "bambu.h"
#include "server.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  // User provided arguments needed within threads.
  char* ip;
  char* device;
  char* passcode;

  // Contexts accessed by the Bambu thread.
  bambu_ctx_t bambu_ctx;
  server_ctx_t server_ctx;

  // Maximum possible size (in bytes) of any frame. Used to allocate enough
  // memory for image buffers.
  size_t image_buffer_size_max;

  // Determines whether to open a connection to the Bambu device and start
  // grabbing frames, e.g., when there is at least one open connection.
  bool run_bambu;
  pthread_cond_t run_bambu_cond;
  pthread_mutex_t run_bambu_mutex;
} thread_ctx_t;


static void* bambu_routine(void* ctx) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) ctx;
  bambu_ctx_t bambu_ctx = thread_ctx->bambu_ctx;
  server_ctx_t server_ctx = thread_ctx->server_ctx;

  while (1) {
    pthread_mutex_lock(&thread_ctx->run_bambu_mutex);
    while (!thread_ctx->run_bambu) {
      pthread_cond_wait(&thread_ctx->run_bambu_cond,
                        &thread_ctx->run_bambu_mutex);
    }
    pthread_mutex_unlock(&thread_ctx->run_bambu_mutex);

    int res = bambu_connect(bambu_ctx, thread_ctx->ip, thread_ctx->device,
                            thread_ctx->passcode);
    if (res < 0) {
      fprintf(stderr, "Error connecting via bambu\n");
      return NULL;
    }

    int fps = bambu_get_framerate(bambu_ctx);
    while (1) {
      pthread_mutex_lock(&thread_ctx->run_bambu_mutex);
      if (!thread_ctx->run_bambu) {
        pthread_mutex_unlock(&thread_ctx->run_bambu_mutex);
        break;  // Stop grabbing frames and disconnect.
      }
      pthread_mutex_unlock(&thread_ctx->run_bambu_mutex);

      uint8_t* bambu_buffer = NULL;
      size_t bambu_buffer_size;
      res = bambu_get_frame(bambu_ctx, &bambu_buffer, &bambu_buffer_size);
      if (res < 0) {
        fprintf(stderr, "Error getting frame\n");
        return NULL;
      }

      if (thread_ctx->image_buffer_size_max < bambu_buffer_size) {
        fprintf(stderr, "Destination image buffer is too small: %ld < %ld\n",
                thread_ctx->image_buffer_size_max, bambu_buffer_size);
        return NULL;
      }

      server_send_image(server_ctx, bambu_buffer, bambu_buffer_size);
      usleep(1000 * 1000 / fps);  // Wait for the next frame tick.
    }
    bambu_disconnect(bambu_ctx);
  }

  return NULL;
}

static void on_client_change(void* callback_ctx, size_t client_count) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) callback_ctx;

#ifdef DEBUG
  fprintf(stderr, "Number of clients changed to: %ld\n", client_count);
#endif

  pthread_mutex_lock(&thread_ctx->run_bambu_mutex);
  thread_ctx->run_bambu = client_count > 0;
  if (thread_ctx->run_bambu) {
    pthread_cond_signal(&thread_ctx->run_bambu_cond);
  }
  pthread_mutex_unlock(&thread_ctx->run_bambu_mutex);
}

int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <ip> <device> <passcode> <port>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  int server_port = atoi(argv[4]);

  bambu_ctx_t bambu_ctx = NULL;
  server_ctx_t server_ctx = NULL;
  int res;

  res = bambu_alloc_ctx(&bambu_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating bambu\n");
    goto close_and_exit;
  }

  res = server_alloc_ctx(&server_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating server\n");
    goto close_and_exit;
  }

  // Connect once to store frame information in bambu_ctx, then close.
  res = bambu_connect(bambu_ctx, ip, device, passcode);
  if (res < 0) {
    fprintf(stderr, "Error connecting via bambu\n");
    goto close_and_exit;
  }
  res = bambu_disconnect(bambu_ctx);
  if (res < 0) {
    fprintf(stderr, "Error disconnecting from bambu\n");
    goto close_and_exit;
  }

  size_t buffer_size = bambu_get_max_frame_buffer_size(bambu_ctx);
  pthread_t bambu_thread;
  thread_ctx_t thread_ctx = {
    .ip = ip,
    .device = device,
    .passcode = passcode,
    .bambu_ctx = bambu_ctx,
    .server_ctx = server_ctx,
    .image_buffer_size_max = buffer_size,
    .run_bambu = false,
    .run_bambu_cond = PTHREAD_COND_INITIALIZER,
    .run_bambu_mutex = PTHREAD_MUTEX_INITIALIZER,
  };

  server_callbacks_t server_callbacks = {
    .callback_ctx = &thread_ctx,
    .on_client_change = on_client_change,
  };

  res = pthread_create(&bambu_thread, NULL, &bambu_routine, &thread_ctx);
  if (res != 0) {
    fprintf(stderr, "Error creating bambu thread\n");
    goto close_and_exit;
  }

  int fps = bambu_get_framerate(bambu_ctx);
  int width = bambu_get_frame_width(bambu_ctx);
  int height = bambu_get_frame_height(bambu_ctx);
  res = server_start(server_ctx, server_port, &server_callbacks, width, height,
                     fps, buffer_size);
  if (res < 0) {
    fprintf(stderr, "Error running server\n");
    goto close_and_exit;
  }

  res = pthread_join(bambu_thread, NULL);
  if (res != 0) {
    fprintf(stderr, "Error joining bambu thread\n");
    goto close_and_exit;
  }

close_and_exit:
  if (server_ctx) {
    server_stop(server_ctx);
    server_free_ctx(server_ctx);
  }
  if (bambu_ctx) {
    bambu_disconnect(bambu_ctx);
    bambu_free_ctx(bambu_ctx);
  }
  return res;
}
