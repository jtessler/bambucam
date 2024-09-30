#include "bambu.h"
#include "server.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  // User provided arguments needed within threads.
  int server_port;

  // The contexts used by both the Bambu and server threads.
  bambu_ctx_t bambu_ctx;
  server_ctx_t server_ctx;
  server_callbacks_t server_callbacks;

  // An image frame buffer shared by both threads. The Bambu thread
  // populates it and the server thread consumes it.
  uint8_t* image_buffer;
  size_t image_buffer_size_max;
  size_t image_size;
  pthread_mutex_t image_buffer_mutex;  // Ensure no concurrent access.
} thread_ctx_t;

// Simply copies the image buffer from the thread context into the given buffer
// after acquiring the mutex lock.
static size_t copy_image_buffer(void* callback_ctx,
                         uint8_t* buffer, size_t buffer_size_max) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) callback_ctx;

  if (buffer_size_max < thread_ctx->image_buffer_size_max) {
    fprintf(stderr, "Destination image buffer is too small: %ld < %ld\n",
            buffer_size_max, thread_ctx->image_buffer_size_max);
    return -1;
  }

  pthread_mutex_lock(&thread_ctx->image_buffer_mutex);
  size_t image_size = thread_ctx->image_size;
  memcpy(buffer, thread_ctx->image_buffer, image_size);
  pthread_mutex_unlock(&thread_ctx->image_buffer_mutex);
  return image_size;
}


static void* bambu_routine(void* ctx) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) ctx;
  bambu_ctx_t bambu_ctx = thread_ctx->bambu_ctx;
  int fps = bambu_get_framerate(bambu_ctx);
  int res;

  while (1) {
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

    pthread_mutex_lock(&thread_ctx->image_buffer_mutex);
    memcpy(thread_ctx->image_buffer, bambu_buffer, bambu_buffer_size);
    thread_ctx->image_size = bambu_buffer_size;
    pthread_mutex_unlock(&thread_ctx->image_buffer_mutex);

    usleep(1000 * 1000 / fps);  // Wait for the next frame tick.
  }

  return NULL;
}

static void* server_routine(void* ctx) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) ctx;
  bambu_ctx_t bambu_ctx = thread_ctx->bambu_ctx;
  server_ctx_t server_ctx = thread_ctx->server_ctx;

  int fps = bambu_get_framerate(bambu_ctx);
  int width = bambu_get_frame_width(bambu_ctx);
  int height = bambu_get_frame_height(bambu_ctx);
  size_t buffer_size = bambu_get_max_frame_buffer_size(bambu_ctx);

  int res = server_start(server_ctx,
                         thread_ctx->server_port,
                         &thread_ctx->server_callbacks,
                         width, height, fps, buffer_size);
  if (res < 0) {
    fprintf(stderr, "Error running server\n");
  }
  return NULL;
}

static void on_client_change(void* callback_ctx, size_t client_count) {
  fprintf(stderr, "Number of clients changed to: %zu\n", client_count);
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

  res = bambu_connect(bambu_ctx, ip, device, passcode);
  if (res < 0) {
    fprintf(stderr, "Error connecting via bambu\n");
    goto close_and_exit;
  }
  size_t buffer_size = bambu_get_max_frame_buffer_size(bambu_ctx);

  pthread_t bambu_thread;
  pthread_t server_thread;
  thread_ctx_t thread_ctx = {
    .server_port = server_port,
    .bambu_ctx = bambu_ctx,
    .server_ctx = server_ctx,
    .server_callbacks = {
      .callback_ctx = &thread_ctx,
      .fill_image_buffer = copy_image_buffer,
      .on_client_change = on_client_change,
    },
    .image_buffer = malloc(buffer_size),
    .image_buffer_size_max = buffer_size,
    .image_size = 0,
    .image_buffer_mutex = PTHREAD_MUTEX_INITIALIZER,
  };

  if (thread_ctx.image_buffer == NULL) {
    fprintf(stderr, "Error allocating shared buffer: %s\n", strerror(errno));
    res = -errno;
    goto close_and_exit;
  }

  res = pthread_create(&bambu_thread, NULL, &bambu_routine, &thread_ctx);
  if (res != 0) {
    fprintf(stderr, "Error creating bambu thread\n");
    goto close_and_exit;
  }

  // TODO: Ensure the image buffer contains real image data before kicking off
  // the server thread.

  res = pthread_create(&server_thread, NULL,
                       &server_routine, &thread_ctx);
  if (res != 0) {
    fprintf(stderr, "Error creating server thread\n");
    goto close_and_exit;
  }

  res = pthread_join(server_thread, NULL);
  if (res != 0) {
    fprintf(stderr, "Error joining server thread\n");
    goto close_and_exit;
  }

  res = pthread_join(bambu_thread, NULL);
  if (res != 0) {
    fprintf(stderr, "Error joining bambu thread\n");
    goto close_and_exit;
  }


close_and_exit:
  if (server_ctx) server_free_ctx(server_ctx);
  if (bambu_ctx) bambu_free_ctx(bambu_ctx);
  if (thread_ctx.image_buffer) free(thread_ctx.image_buffer);
  return res;
}
