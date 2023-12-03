// RTP server to stream camera frames
//
// Manages an RTP server and encodes images into a video stream.

#include <stddef.h>
#include <stdint.h>

// Opaque pointer to the underling RTP server implementation. The caller owns
// this object.
typedef struct rtp_server_ctx* rtp_server_ctx_t;

// Allocates the objects required to open an RTP stream. The caller is expected
// to call rtp_server_free_ctx when done with it.
int rtp_server_alloc_ctx(rtp_server_ctx_t* ctx);
int rtp_server_free_ctx(rtp_server_ctx_t ctx);

typedef struct {
  // Opaque pointer to callback context of the caller's choosing. Passed as an
  // argument in all callbacks.
  void* callback_ctx;

  // Called when the RTP server needs an image to encode and send in the video
  // feed. The callee should fill the buffer with image data and return the
  // number of copied bytes. The callee should return a negative value on error
  // or if max_size is smaller than the amount data needed to copy.
  size_t (*fill_image_buffer)(void* callback_ctx,
                              uint8_t* buffer, size_t max_size);
} rtp_server_callbacks_t;

// Starts the RTP server at the given port with the given video stream details.
// Calls fill_image_buffer to fill a buffer with image data at regular
// intervals based on the given frame rate (fps).
//
// The caller should provide the maximum possible size of a single frame in
// buffer_size, which the RTP server uses to allocate and own buffer memory.
// This memory is freed via rtp_server_free_ctx.
//
// The RTP server is expected to run indefinitely, so this function is not
// expected to return. It returns a negative value on error.
int rtp_server_start(rtp_server_ctx_t ctx,
                     int port, rtp_server_callbacks_t* callbacks,
                     int width, int height, int fps, size_t buffer_size);
