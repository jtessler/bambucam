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

  // Called when the server needs an image to encode and send in the video
  // feed. The callee should fill the buffer with image data and return the
  // number of copied bytes. The callee should return a negative value on error
  // or if max_size is smaller than the amount data needed to copy.
  size_t (*fill_image_buffer)(void* callback_ctx,
                              uint8_t* buffer, size_t max_size);

  // Called whenever a client connects or disconnects from the server. The
  // number of active connections is passed as an argument.
  void (*on_client_change)(void* callback_ctx, size_t client_count);
} server_callbacks_t;

// Starts the server at the given port with the given video stream details.
// Calls fill_image_buffer to fill a buffer with image data at regular
// intervals based on the given frame rate (fps).
//
// The caller should provide the maximum possible size of a single frame in
// buffer_size, which the server uses to allocate and own buffer memory. This
// memory is freed via server_free_ctx.
//
// The server is expected to run indefinitely, so this function is not expected
// to return. It returns a negative value on error.
int server_start(server_ctx_t ctx,
                 int port, server_callbacks_t* callbacks,
                 int width, int height, int fps, size_t buffer_size);
