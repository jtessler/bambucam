// Generic server interface to stream camera frames
//
// Manages a server and how it handles incoming frames to serve a video stream
// at the given port.

#include <stddef.h>
#include <stdint.h>

// Opaque pointer to the underling server implementation (e.g., RTP or HTTP).
// The caller owns this object.
typedef struct server_ctx* server_ctx_t;

// Allocates the objects required to start the video streaming server. The
// caller is expected to call server_free_ctx when done with it.
int server_alloc_ctx(server_ctx_t* ctx);
int server_free_ctx(server_ctx_t ctx);

typedef struct {
  // Opaque pointer to callback context of the caller's choosing. Passed as an
  // argument in all callbacks.
  void* callback_ctx;

  // Called whenever a client connects or disconnects from the server. The
  // number of active connections is passed as an argument.
  void (*on_client_change)(void* callback_ctx, size_t client_count);
} server_callbacks_t;

// Starts the server at the given port with the given video stream details.
//
// The caller should provide the maximum possible size of a single frame in
// buffer_size, which the server uses to allocate and own buffer memory. This
// memory is freed via server_free_ctx.
//
// Returns zero if the server successfully started on a separtes thread.
// Returns a negative value on error.
int server_start(server_ctx_t ctx,
                 int port, server_callbacks_t* callbacks,
                 int width, int height, int fps, size_t buffer_size);
int server_stop(server_ctx_t ctx);

// Sends the provided image to all active clients.
int server_send_image(server_ctx_t ctx, uint8_t* buffer, size_t size);
