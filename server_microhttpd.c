// HTTP server implementation using microhttpd to forward JPEG frames into an
// MJPEG data stream.

#include "server.h"

#include <errno.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

// The string separating each frame in the multipart/x-mixed-replace response.
#define BOUNDARY "boundary"

// The maximum chunk size for each response.
#define RESPONSE_BLOCK_SIZE_BYTES (128 * 1024)

// Internal bookkeeping state for the HTTP server.
typedef struct {
  // Expected frames per second. Used to calculate how long to sleep before
  // requesting a new frame using the above callback function.
  int fps;

  // Pointer to the server callbacks to use when creating an HTTP response.
  server_callbacks_t* callbacks;

  // Image buffer and its max size (as allocated by this file).
  uint8_t* image_buffer;
  size_t image_buffer_size;

  // Frame counter to know when serving the first frame and logging.
  ssize_t frame_i;

  // Current frame's starting position in the ever-growing multipart response,
  // because it might get chucked and we need to know how far into the image
  // buffer we need to seek.
  //
  // A negative value means the current frame is completely sent and the next
  // frame should be fetched from the above callback function.
  ssize_t frame_start_pos;

  // Current frame's size (always less than or equal to image_buffer_size).
  ssize_t frame_size;

  // Number of active client connections.
  size_t num_connections;
} ctx_internal_t;

int server_alloc_ctx(server_ctx_t* ctx) {
  *ctx = malloc(sizeof(ctx_internal_t));
  if (*ctx == NULL) {
    fprintf(stderr, "Error allocating context: %s\n", strerror(errno));
    return -errno;
  }

  memset(*ctx, 0, sizeof(ctx_internal_t));
  return 0;
}

int server_free_ctx(server_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  if (ctx_internal->image_buffer) {
    free(ctx_internal->image_buffer);
  }
  free(ctx_internal);
  return 0;
}

static ssize_t response_callback(void* ctx, uint64_t pos,
                                 char* buf, size_t max) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;

  // A negative starting position means we need to fetch a new frame, write the
  // multipart boundary, then start sending new data (possibly chunked).
  if (ctx_internal->frame_start_pos < 0) {
    ctx_internal->frame_size = ctx_internal->callbacks->fill_image_buffer(
        ctx_internal->callbacks->callback_ctx,
        ctx_internal->image_buffer,
        ctx_internal->image_buffer_size);
    if (ctx_internal->frame_size < 0) {
      fprintf(stderr, "Error getting frame #%ld\n", ctx_internal->frame_i);
      return -1;
    }
    if (ctx_internal->frame_size == 0) {
      fprintf(stderr, "Received empty frame, skipping\n");
      usleep(1000 * 1000 / ctx_internal->fps);
      return 0;
    }

#ifdef DEBUG
    fprintf(stderr, "Frame #%ld (%ld bytes)\n",
            ctx_internal->frame_i, ctx_internal->frame_size);
#endif

    int res = snprintf(buf, max,
                       "%s"  // Special case for first frame.
                       "Content-Type: image/jpeg\r\n"
                       "Content-Length: %ld\r\n\r\n",
                       ctx_internal->frame_i == 0 ? "--" BOUNDARY "\r\n" : "",
                       ctx_internal->frame_size);
    ctx_internal->frame_start_pos = pos + res;
    return res;
  }

  size_t frame_offset = pos - ctx_internal->frame_start_pos;
  if (frame_offset >= ctx_internal->frame_size) {
    int res = snprintf(buf, max, "\r\n--%s\r\n", BOUNDARY);
    ctx_internal->frame_start_pos = -1;
    ctx_internal->frame_i++;
    usleep(1000 * 1000 / ctx_internal->fps);
    return res;
  }

  size_t size = MIN(ctx_internal->frame_size - frame_offset, max);
  memcpy(buf, ctx_internal->image_buffer + frame_offset, size);
  return size;
}

static enum MHD_Result default_handler(void* ctx,
                                       struct MHD_Connection *connection,
                                       const char *url,
                                       const char *method,
                                       const char *version,
                                       const char *upload_data,
                                       size_t *upload_data_size,
                                       void **con_cls) {
  struct MHD_Response* response;
  enum MHD_Result res;

  if (strcmp(url, "/") != 0 || strcmp(method, "GET") != 0) {
    fprintf(stderr, "Only handling GET /\n");
    response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    res = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return res;
  }

  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  ctx_internal->frame_i = 0;
  ctx_internal->frame_start_pos = -1;
  response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN,
                                               RESPONSE_BLOCK_SIZE_BYTES,
                                               response_callback, ctx,
                                               NULL);  // No free callback.
  if (!response) {
    fprintf(stderr, "Error generating response\n");
    return MHD_NO;
  }

  res = MHD_add_response_header(response,
                                MHD_HTTP_HEADER_CONTENT_TYPE,
                                "multipart/x-mixed-replace;boundary=" BOUNDARY);
  if (res != MHD_YES) {
    fprintf(stderr, "Error setting content type: multipart/x-mixed-replace\n");
    return res;
  }

  res = MHD_set_response_options(response,
                                 MHD_RF_HTTP_1_0_COMPATIBLE_STRICT,
                                 MHD_RO_END);
  if (res != MHD_YES) {
    fprintf(stderr, "Error setting response option\n");
    return res;
  }

  res = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return res;
}

static void on_connection_change(void* ctx, struct MHD_Connection *connection,
                                 void** socket_context,
                                 enum MHD_ConnectionNotificationCode code) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  switch (code) {
  case MHD_CONNECTION_NOTIFY_STARTED:
    ctx_internal->num_connections++;
    break;
  case MHD_CONNECTION_NOTIFY_CLOSED:
    ctx_internal->num_connections--;
    break;
  }
  ctx_internal->callbacks->on_client_change(
      ctx_internal->callbacks->callback_ctx,
      ctx_internal->num_connections);
}

int server_start(server_ctx_t ctx,
                 int port, server_callbacks_t* callbacks,
                 int width, int height, int fps, size_t buffer_size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  ctx_internal->num_connections = 0;
  ctx_internal->fps = fps;
  ctx_internal->callbacks = callbacks;
  ctx_internal->image_buffer_size = buffer_size;
  ctx_internal->image_buffer = malloc(ctx_internal->image_buffer_size);
  if (ctx_internal->image_buffer == NULL) {
    fprintf(stderr, "Error allocating image buffer: %s\n", strerror(errno));
    return -errno;
  }

  // To avoid every connection racing to fill the same image buffer, allow only
  // a single connection.
  //
  // TODO: Redesign how the buffer is used to support multiple connections.
  enum MHD_FLAG flags = MHD_USE_EPOLL_INTERNAL_THREAD | MHD_USE_ERROR_LOG;
#ifdef DEBUG
  flags |= MHD_USE_DEBUG;
#endif
  struct MHD_Daemon* daemon = MHD_start_daemon(flags, port,
                                               NULL, NULL,  // Accept all IPs.
                                               &default_handler, ctx,
                                               MHD_OPTION_CONNECTION_LIMIT, 1,
                                               MHD_OPTION_NOTIFY_CONNECTION,
                                               on_connection_change, ctx,
                                               MHD_OPTION_END);
  if (!daemon) {
    fprintf(stderr, "Error starting MHD daemon\n");
    return -1;
  }

  fprintf(stderr, "Serving video stream at: http://localhost:%d/\n", port);

  while (1) {
    sleep(5);
#ifdef DEBUG
    fprintf(stderr, "Number of active connections: %d\n",
            ctx_internal->num_connections);
#endif
  }

  MHD_stop_daemon(daemon);
  return 0;
}
