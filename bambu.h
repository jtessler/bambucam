// Bambu camera handler
//
// A wrapper around the Bambu tunnel prebuilt library, which manages the local
// network connection to a Bambu 3D printer. Exposes functions to load a single
// camera frame into a buffer.

#include <stddef.h>
#include <stdint.h>

// Opaque pointer to prebuilt Bambu structures to maintain a single network
// connection with a Bambu 3D printer. The caller owns this object.
typedef struct bambu_ctx* bambu_ctx_t;

// Allocates the objects required to open a network connection with a Bambu 3D
// printer. The caller is expected to call bambu_free_ctx when done with it.
int bambu_alloc_ctx(bambu_ctx_t* ctx);
int bambu_free_ctx(bambu_ctx_t ctx);

// Opens a new connection and camera stream to a Bambu 3D printer in LAN Mode
// based on:
//
//   1. IP address or hostname
//   2. Device identifier, which is likely the serial number
//   3. Access code generated in the Bambu network UI on the printer
//
// See: https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode
int bambu_connect(bambu_ctx_t ctx, char* ip, char* device, char* passcode);
int bambu_disconnect(bambu_ctx_t ctx);

//
// The following functions assume a connection is established.
//

// Returns the maximum possible frame buffer size in bytes.
size_t bambu_get_max_frame_buffer_size(bambu_ctx_t ctx);

// Returns the framerate in frames-per-second (FPS).
int bambu_get_framerate(bambu_ctx_t ctx);

// Returns the frame dimensions.
int bambu_get_frame_width(bambu_ctx_t ctx);
int bambu_get_frame_height(bambu_ctx_t ctx);

// Fetches a frame and passes the underlying image buffer in the given
// arguments. The caller does NOT own this buffer and should make its own copy.
int bambu_get_frame(bambu_ctx_t ctx, uint8_t** buffer, size_t* size);
