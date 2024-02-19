#include "bambucam.h"
#include "rtp_server.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  // User provided arguments needed within threads.
  int rtp_port;

  // The contexts used by both the bambucam and RTP server threads.
  bambucam_ctx_t bambucam_ctx;
  rtp_server_ctx_t rtp_server_ctx;
  rtp_server_callbacks_t rtp_server_callbacks;

  // An image frame buffer shared by both threads. The bambucam thread
  // populates it and the RTP server thread consumes it.
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
  memcpy(buffer, thread_ctx->image_buffer, thread_ctx->image_size);
  pthread_mutex_unlock(&thread_ctx->image_buffer_mutex);

  return thread_ctx->image_size;
}


static void* bambucam_routine(void* ctx) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) ctx;
  bambucam_ctx_t bambucam_ctx = thread_ctx->bambucam_ctx;
  int fps = bambucam_get_framerate(bambucam_ctx);
  int res;

  while (1) {
    uint8_t* bambucam_buffer = NULL;
    size_t bambucam_buffer_size;
    res = bambucam_get_frame(bambucam_ctx,
                             &bambucam_buffer,
                             &bambucam_buffer_size);
    if (res < 0) {
      fprintf(stderr, "Error getting frame\n");
      return NULL;
    }

    if (thread_ctx->image_buffer_size_max < bambucam_buffer_size) {
      fprintf(stderr, "Destination image buffer is too small: %ld < %ld\n",
              thread_ctx->image_buffer_size_max, bambucam_buffer_size);
      return NULL;
    }

    pthread_mutex_lock(&thread_ctx->image_buffer_mutex);
    memcpy(thread_ctx->image_buffer, bambucam_buffer, bambucam_buffer_size);
    thread_ctx->image_size = bambucam_buffer_size;
    pthread_mutex_unlock(&thread_ctx->image_buffer_mutex);

    usleep(1000 * 1000 / fps);  // Wait for the next frame tick.
  }

  return NULL;
}

static void* rtp_server_routine(void* ctx) {
  thread_ctx_t* thread_ctx = (thread_ctx_t*) ctx;
  bambucam_ctx_t bambucam_ctx = thread_ctx->bambucam_ctx;
  rtp_server_ctx_t rtp_server_ctx = thread_ctx->rtp_server_ctx;

  int fps = bambucam_get_framerate(bambucam_ctx);
  int width = bambucam_get_frame_width(bambucam_ctx);
  int height = bambucam_get_frame_height(bambucam_ctx);
  size_t buffer_size = bambucam_get_max_frame_buffer_size(bambucam_ctx);

  int res = rtp_server_start(rtp_server_ctx,
                             thread_ctx->rtp_port,
                             &thread_ctx->rtp_server_callbacks,
                             width, height, fps, buffer_size);
  if (res < 0) {
    fprintf(stderr, "Error running RTP server\n");
  }
  return NULL;
}

int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <ip> <device> <passcode> <rtp-port>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  int rtp_port = atoi(argv[4]);

  bambucam_ctx_t bambucam_ctx = NULL;
  rtp_server_ctx_t rtp_server_ctx = NULL;
  int res;

  res = bambucam_alloc_ctx(&bambucam_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating bambucam\n");
    goto close_and_exit;
  }

  res = rtp_server_alloc_ctx(&rtp_server_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating RTP server\n");
    goto close_and_exit;
  }

  res = bambucam_connect(bambucam_ctx, ip, device, passcode);
  if (res < 0) {
    fprintf(stderr, "Error connecting via bambucam\n");
    goto close_and_exit;
  }
  size_t buffer_size = bambucam_get_max_frame_buffer_size(bambucam_ctx);

  pthread_t bambucam_thread;
  pthread_t rtp_server_thread;
  thread_ctx_t thread_ctx = {
    .rtp_port = rtp_port,
    .bambucam_ctx = bambucam_ctx,
    .rtp_server_ctx = rtp_server_ctx,
    .rtp_server_callbacks = {
      .callback_ctx = &thread_ctx,
      .fill_image_buffer = copy_image_buffer,
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

  res = pthread_create(&bambucam_thread, NULL,
                       &bambucam_routine, &thread_ctx);
  if (res != 0) {
    fprintf(stderr, "Error creating bambucam thread\n");
    goto close_and_exit;
  }

  // TODO: Ensure the image buffer contains real image data before kicking off
  // the RTP server thread.

  res = pthread_create(&rtp_server_thread, NULL,
                       &rtp_server_routine, &thread_ctx);
  if (res != 0) {
    fprintf(stderr, "Error creating RTP server thread\n");
    goto close_and_exit;
  }

  res = pthread_join(rtp_server_thread, NULL);
  if (res != 0) {
    fprintf(stderr, "Error joining RTP server thread\n");
    goto close_and_exit;
  }

  res = pthread_join(bambucam_thread, NULL);
  if (res != 0) {
    fprintf(stderr, "Error joining bambucam thread\n");
    goto close_and_exit;
  }


close_and_exit:
  if (rtp_server_ctx) rtp_server_free_ctx(rtp_server_ctx);
  if (bambucam_ctx) bambucam_free_ctx(bambucam_ctx);
  if (thread_ctx.image_buffer) free(thread_ctx.image_buffer);
  return res;
}
