// HTTP server implementation using microhttpd to forward JPEG frames into an
// MJPEG data stream.

#include "server.h"

#include <errno.h>
#include <microhttpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

// The string separating each frame in the multipart/x-mixed-replace response.
#define BOUNDARY "boundary"

// The maximum chunk size for each response.
#define RESPONSE_BLOCK_SIZE_BYTES (128 * 1024)

// Maximum number of active connections supported
#define MAX_NUM_CONNECTIONS 100

// Special case for frame_start_pos in connection_ctx_t that represents
// end-of-file.
#define FRAME_END_POSITION -1

// Internal bookkeeping state for an individual connection.
typedef struct {
  // Serial number identifier.
  ssize_t id;

  // The underlying microhttpd connection.
  struct MHD_Connection* connection;

  // Grant any connection context access to the server context to access the
  // server state, e.g., the frame buffer.
  server_ctx_t server_ctx;

  // Frame counter to know when serving the first frame and logging.
  ssize_t frame_i;

  // Current frame's starting position in the ever-growing multipart response,
  // because it might get chucked and we need to know how far into the image
  // buffer we need to seek.
  //
  // If set to FRAME_END_POSITION, then the current frame was completely sent
  // and the connection should suspend until the next frame is available.
  ssize_t frame_start_pos;  // TODO: Protect with a mutex.
} connection_ctx_t;

// Internal bookkeeping state for the HTTP server.
typedef struct {
  // Pointer to the server callbacks to use when creating an HTTP response.
  server_callbacks_t* callbacks;

  // Image buffer and its max size (as allocated by this file).
  uint8_t* image_buffer;
  size_t image_buffer_size;
  pthread_mutex_t image_buffer_mutex;

  // Current frame's size (always less than or equal to image_buffer_size).
  ssize_t frame_size;

  // Number of active client connections and underlying state.
  // TODO: Put individual connections on the heap, not this static array.
  size_t num_connections;
  connection_ctx_t connections[MAX_NUM_CONNECTIONS];
  size_t next_connection_id;

  // The underlying microhttpd daemon.
  struct MHD_Daemon* daemon;
} ctx_internal_t;

int server_alloc_ctx(server_ctx_t* ctx) {
  ctx_internal_t* ctx_internal = malloc(sizeof(ctx_internal_t));
  if (ctx_internal == NULL) {
    fprintf(stderr, "Error allocating context: %s\n", strerror(errno));
    return -errno;
  }

  pthread_mutex_t image_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
  memset(ctx_internal, 0, sizeof(ctx_internal_t));
  ctx_internal->image_buffer_mutex = image_buffer_mutex;
  *ctx = (server_ctx_t) ctx_internal;
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
  connection_ctx_t* connection_ctx = (connection_ctx_t*) ctx;
  ctx_internal_t* ctx_internal = (ctx_internal_t*) connection_ctx->server_ctx;

  if (connection_ctx->connection == NULL) {
    fprintf(stderr, "Response callback called with dead connection, ending\n");
    return MHD_CONTENT_READER_END_OF_STREAM;
  }

  if (ctx_internal->frame_size == 0 ||
      connection_ctx->frame_start_pos == FRAME_END_POSITION) {
#ifdef DEBUG
    fprintf(stderr, "Received end of frame on connection %ld, suspending\n",
            connection_ctx->id);
#endif
    MHD_suspend_connection(connection_ctx->connection);
    return 0;
  }

  // If we're at the beginning of a frame, just send headers first.
  if (connection_ctx->frame_start_pos == 0) {
#ifdef DEBUG
    fprintf(stderr, "Connection %ld Frame #%ld (%ld bytes)\n",
            connection_ctx->id, connection_ctx->frame_i,
            ctx_internal->frame_size);
#endif

    int res = snprintf(buf, max,
                       "%s"  // Special case for first frame.
                       "Content-Type: image/jpeg\r\n"
                       "Content-Length: %ld\r\n\r\n",
                       connection_ctx->frame_i == 0 ? "--" BOUNDARY "\r\n" : "",
                       ctx_internal->frame_size);
    if (res < 0) {
      return MHD_CONTENT_READER_END_WITH_ERROR;
    }
    connection_ctx->frame_start_pos = pos + res;
    return res;
  }

  // If we're at the end of a frame, update state and send footer.
  size_t frame_offset = pos - connection_ctx->frame_start_pos;
  if (frame_offset >= ctx_internal->frame_size) {
    int res = snprintf(buf, max, "\r\n--%s\r\n", BOUNDARY);
    if (res < 0) {
      return MHD_CONTENT_READER_END_WITH_ERROR;
    }
    connection_ctx->frame_start_pos = FRAME_END_POSITION;
    connection_ctx->frame_i++;
    return res;
  }

  // Otherwise, attempt to send the entire image buffer data.
  size_t size = MIN(ctx_internal->frame_size - frame_offset, max);
  pthread_mutex_lock(&ctx_internal->image_buffer_mutex);
  memcpy(buf, ctx_internal->image_buffer + frame_offset, size);
  pthread_mutex_unlock(&ctx_internal->image_buffer_mutex);
  return size;
}

static connection_ctx_t* get_connection_ctx(ctx_internal_t* ctx_internal,
                                            struct MHD_Connection *connection) {
  for (int i = 0; i < MAX_NUM_CONNECTIONS; i++) {
    if (ctx_internal->connections[i].connection == connection) {
      return &ctx_internal->connections[i];
    }
  }
  return NULL;
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
  connection_ctx_t* connection_ctx = get_connection_ctx(ctx_internal,
                                                        connection);
  if (connection_ctx == NULL) {
    fprintf(stderr, "Error locating connection state. Too many connections?\n");
    response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    res = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                             response);
    MHD_destroy_response(response);
    return res;
  }

  connection_ctx->frame_i = 0;
  connection_ctx->frame_start_pos = 0;
  response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN,
                                               RESPONSE_BLOCK_SIZE_BYTES,
                                               response_callback,
                                               connection_ctx,
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
  connection_ctx_t* connection_ctx;
  switch (code) {
  case MHD_CONNECTION_NOTIFY_STARTED:
    ctx_internal->num_connections++;
    connection_ctx = get_connection_ctx(ctx_internal, NULL);
    if (connection_ctx == NULL) {
      fprintf(stderr, "Error allocating connection context\n");
    } else {
      connection_ctx->id = ctx_internal->next_connection_id++;
      connection_ctx->connection = connection;
      connection_ctx->server_ctx = (server_ctx_t) ctx;
    }
    break;
  case MHD_CONNECTION_NOTIFY_CLOSED:
    ctx_internal->num_connections--;
    connection_ctx = get_connection_ctx(ctx_internal, connection);
    if (connection_ctx == NULL) {
      fprintf(stderr, "Error locating connection state\n");
    } else {
      memset(connection_ctx, 0, sizeof(connection_ctx_t));
    }
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
  ctx_internal->callbacks = callbacks;
  ctx_internal->image_buffer_size = buffer_size;
  ctx_internal->image_buffer = malloc(ctx_internal->image_buffer_size);
  if (ctx_internal->image_buffer == NULL) {
    fprintf(stderr, "Error allocating image buffer: %s\n", strerror(errno));
    return -errno;
  }

  enum MHD_FLAG flags = MHD_NO_FLAG;
  // Not supported on Darwin? Maybe use poll or just not bother?
  // flags |= MHD_USE_EPOLL_INTERNAL_THREAD;
  // I assume there's some synchronization expectation that we want this. Looks like the API changed so it's not per polling method.
  flags |= MHD_USE_INTERNAL_POLLING_THREAD;
  flags |= MHD_ALLOW_SUSPEND_RESUME;
  flags |= MHD_USE_ERROR_LOG;
#ifdef DEBUG
  flags |= MHD_USE_DEBUG;
#endif
  ctx_internal->daemon = MHD_start_daemon(flags, port,
                                          NULL, NULL,  // Accept all IPs.
                                          &default_handler, ctx,
                                          MHD_OPTION_CONNECTION_LIMIT,
                                          MAX_NUM_CONNECTIONS,
                                          MHD_OPTION_NOTIFY_CONNECTION,
                                          on_connection_change, ctx,
                                          MHD_OPTION_END);
  if (!ctx_internal->daemon) {
    fprintf(stderr, "Error starting MHD daemon\n");
    return -1;
  }

  fprintf(stderr, "Serving video stream at: http://localhost:%d/\n", port);
  return 0;
}

int server_stop(server_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  if (ctx_internal->daemon == NULL) {
    fprintf(stderr, "Attempting to close an uninitialized server\n");
    return -1;
  }
  MHD_stop_daemon(ctx_internal->daemon);
  return 0;
}

int server_send_image(server_ctx_t ctx, uint8_t* buffer, size_t size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  if (size > ctx_internal->image_buffer_size) {
    fprintf(stderr, "Image buffer too large: %ld > %ld\n", size,
            ctx_internal->image_buffer_size);
    return -1;
  }

  pthread_mutex_lock(&ctx_internal->image_buffer_mutex);
  memcpy(ctx_internal->image_buffer, buffer, size);
  ctx_internal->frame_size = size;
  pthread_mutex_unlock(&ctx_internal->image_buffer_mutex);

  for (int i = 0; i < MAX_NUM_CONNECTIONS; i++) {
    connection_ctx_t* connection_ctx = &ctx_internal->connections[i];
    if (connection_ctx->connection == NULL) {
      continue;  // Skip inactive connection contexts.
    }

    if (connection_ctx->frame_start_pos != FRAME_END_POSITION) {
      fprintf(stderr, "Sending frame before connection %ld is ready\n",
              connection_ctx->id);
    }
    connection_ctx->frame_start_pos = 0;

    const union MHD_ConnectionInfo* info;
    info = MHD_get_connection_info(connection_ctx->connection,
                                   MHD_CONNECTION_INFO_CONNECTION_SUSPENDED);
    if (info == NULL) {
      fprintf(stderr, "Error fetching connection %ld info\n",
              connection_ctx->id);
      return -1;
    }
    if (info->suspended == MHD_YES) {
      MHD_resume_connection(connection_ctx->connection);
#ifdef DEBUG
      fprintf(stderr, "Resumed connection %ld\n", connection_ctx->id);
#endif
    }
  }
  return 0;
}
