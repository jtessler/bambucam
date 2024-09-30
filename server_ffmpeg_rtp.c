// RTP server implementation using FFmpeg to transcode JPEG frames into an MPEG
// video and stream it using the Pro-MPEG Code of Practice #3 Release 2 FEC
// protocol.

#include "server.h"

#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <unistd.h>

#define URL_MAX_SIZE 2048
#define URL_OUTPUT_FORMAT "rtp://localhost:%d"

// The internal FFmpeg objects that make up the RTP server context.
typedef struct {
  // Input objects used to parse image data, decode it, and prepare it for
  // encoding into an RTP video stream.
  AVCodecParserContext* parser_ctx;
  AVCodecContext* decoder_ctx;
  const AVCodec* decoder_codec;

  // Output objects used to encode raw image data, format it, and send it to
  // the RTP output stream.
  AVCodecContext* encoder_ctx;
  const AVCodec* encoder_codec;
  AVIOContext* output_ctx;
  AVFormatContext* output_format_ctx;
  AVStream* output_stream;

  // Intermediary objects used in decoding and encoding.
  AVPacket* packet;
  AVFrame* frame;
  uint8_t* image_buffer;
  size_t image_buffer_size;
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

  if (ctx_internal->output_ctx) {
    avio_closep(&ctx_internal->output_ctx);
  }
  if (ctx_internal->output_format_ctx) {
    avformat_free_context(ctx_internal->output_format_ctx);
  }
  if (ctx_internal->parser_ctx) {
    av_parser_close(ctx_internal->parser_ctx);
  }
  if (ctx_internal->encoder_ctx) {
    avcodec_free_context(&ctx_internal->encoder_ctx);
  }
  if (ctx_internal->decoder_ctx) {
    avcodec_free_context(&ctx_internal->decoder_ctx);
  }
  if (ctx_internal->frame) {
    av_frame_free(&ctx_internal->frame);
  }
  if (ctx_internal->packet) {
    av_packet_free(&ctx_internal->packet);
  }
  if (ctx_internal->image_buffer) {
    free(ctx_internal->image_buffer);
  }

  free(ctx_internal);
  return 0;
}

// Decodes the image located in the context's image_buffer field. Uses the
// packet field as an intermediary object and fills the frame field with the
// result (a decoded image frame).
static int create_video_frame(ctx_internal_t* ctx_internal, size_t image_size) {
  uint8_t* buffer_ptr = ctx_internal->image_buffer;
  int res;

  do {
    res = av_parser_parse2(ctx_internal->parser_ctx,
                           ctx_internal->decoder_ctx,
                           &ctx_internal->packet->data,
                           &ctx_internal->packet->size,
                           buffer_ptr, image_size,
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (res < 0) {
      fprintf(stderr, "Error creating image packet: %s\n", av_err2str(res));
      return res;
    }
    buffer_ptr += res;
    image_size -= res;
  } while (ctx_internal->packet->size == 0);  // Loop until the packet is ready.

  res = avcodec_send_packet(ctx_internal->decoder_ctx,
                            ctx_internal->packet);
  if (res < 0) {
    fprintf(stderr, "Error sending image packet: %s\n", av_err2str(res));
    return res;
  }

  res = avcodec_receive_frame(ctx_internal->decoder_ctx,
                              ctx_internal->frame);
  if (res < 0 && res != AVERROR_EOF) {
    fprintf(stderr, "Error receiving decoded image frame: %s\n", av_err2str(res));
    return res;
  }

  av_packet_unref(ctx_internal->packet);
  return 0;
}

// Encodes and sends the video frame located in the context's frame field. Uses
// the packet field as an intermediary object.
//
// If the is_flush flag is set, this function sends a null packet to the output
// stream to signal end-of-stream.
static int send_video_frame(ctx_internal_t* ctx_internal, int is_flush) {
  int res;

  res = avcodec_send_frame(ctx_internal->encoder_ctx,
                           is_flush ? NULL : ctx_internal->frame);
  if (res < 0) {
    fprintf(stderr, "Error sending a frame to encoder\n");
    return res;
  }

  do {
    res = avcodec_receive_packet(ctx_internal->encoder_ctx,
                                 ctx_internal->packet);
    switch (res) {
    case 0:
      break; // Fall through to send encoded packet.
    case AVERROR(EAGAIN):
    case AVERROR_EOF:
      av_frame_unref(ctx_internal->frame);
      return 0; // Return success when there's nothing left to send.
    default:
      fprintf(stderr, "Error receiving encoder result\n");
      return res;
    }

    av_packet_rescale_ts(ctx_internal->packet,
                         ctx_internal->encoder_ctx->time_base,
                         ctx_internal->output_stream->time_base);
    res = av_write_frame(ctx_internal->output_format_ctx, ctx_internal->packet);
    if (res < 0) {
      fprintf(stderr, "Error writing frame to output stream: %s\n", av_err2str(res));
      return res;
    }
    av_packet_unref(ctx_internal->packet);
  } while (1);  // Loop until there's no more encoded packets to send.
}


int server_start(server_ctx_t ctx,
                 int port, server_callbacks_t* callbacks,
                 int width, int height, int fps, size_t buffer_size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  char out_url[URL_MAX_SIZE];
  int res;

  res = snprintf(out_url, URL_MAX_SIZE, URL_OUTPUT_FORMAT, port);
  if (res < 0) {
    fprintf(stderr, "Error formatting output URL\n");
    return res;
  }

#ifdef DEBUG
  av_log_set_level(AV_LOG_DEBUG);
#endif

  // TODO: Wait for a client connection before writing any data and support
  // more than one connection.
  callbacks->on_client_change(callbacks->callback_ctx, 1);

  //
  // Initialize the many many FFmpeg objects needed to produce a video stream.
  //

  res = avio_open(&ctx_internal->output_ctx, out_url, AVIO_FLAG_WRITE);
  if (res < 0) {
    fprintf(stderr, "Error opening output context %s: %s\n",
            out_url, av_err2str(res));
    return res;
  }

  ctx_internal->output_format_ctx = avformat_alloc_context();
  if (!ctx_internal->output_format_ctx) {
    fprintf(stderr, "Error allocating output format context\n");
    return -1;
  }
  ctx_internal->output_format_ctx->pb = ctx_internal->output_ctx;

  ctx_internal->output_format_ctx->oformat = av_guess_format("rtp_mpegts",
                                                             NULL, NULL);
  if (!ctx_internal->output_format_ctx->oformat) {
    fprintf(stderr, "Error guessing output format: %s\n", out_url);
    return -1;
  }

  ctx_internal->output_format_ctx->url = av_strdup(out_url);
  if (!ctx_internal->output_format_ctx->url) {
    fprintf(stderr, "Error allocating URL: %s\n", out_url);
    return -1;
  }

  ctx_internal->encoder_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
  if (!ctx_internal->encoder_codec) {
    fprintf(stderr, "Encoder codec not found\n");
    return -1;
  }

  ctx_internal->decoder_codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
  if (!ctx_internal->encoder_codec) {
    fprintf(stderr, "Decoder codec not found\n");
    return -1;
  }

  ctx_internal->output_stream =
      avformat_new_stream(ctx_internal->output_format_ctx, NULL);
  if (!ctx_internal->output_stream) {
    fprintf(stderr, "Error creating output stream\n");
    return -1;
  }

  ctx_internal->encoder_ctx =
      avcodec_alloc_context3(ctx_internal->encoder_codec);
  if (!ctx_internal->encoder_ctx) {
    fprintf(stderr, "Error allocating encoder codec context\n");
    return -1;
  }

  ctx_internal->decoder_ctx =
      avcodec_alloc_context3(ctx_internal->decoder_codec);
  if (!ctx_internal->decoder_ctx) {
    fprintf(stderr, "Error allocating decoder codec context\n");
    return -1;
  }

  ctx_internal->parser_ctx = av_parser_init(ctx_internal->decoder_codec->id);
  if (!ctx_internal->parser_ctx) {
    fprintf(stderr, "Error initializing decoder parser context\n");
    return -1;
  }

  //
  // Configure the video encoder and output stream.
  //

  av_channel_layout_default(&ctx_internal->encoder_ctx->ch_layout, 1);
  ctx_internal->encoder_ctx->width = width;
  ctx_internal->encoder_ctx->height = height;
  ctx_internal->encoder_ctx->bit_rate = width * height * 4;
  ctx_internal->encoder_ctx->time_base = (AVRational) { 1, fps };
  ctx_internal->encoder_ctx->framerate = (AVRational) { fps, 1 };
  ctx_internal->encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

  if (ctx_internal->output_format_ctx->flags & AVFMT_GLOBALHEADER)
    ctx_internal->encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  ctx_internal->output_stream->time_base = ctx_internal->encoder_ctx->time_base;

  //
  // Open the encoders and allocate intermediary objects.
  //

  res = avcodec_open2(ctx_internal->encoder_ctx,
                      ctx_internal->encoder_codec,
                      NULL);
  if (res < 0) {
    fprintf(stderr, "Error opening encoder codec: %s\n", av_err2str(res));
    return res;
  }

  res = avcodec_open2(ctx_internal->decoder_ctx,
                      ctx_internal->decoder_codec,
                      NULL);
  if (res < 0) {
    fprintf(stderr, "Error opening decoder codec: %s\n", av_err2str(res));
    return res;
  }

  res = avcodec_parameters_from_context(ctx_internal->output_stream->codecpar,
                                        ctx_internal->encoder_ctx);
  if (res < 0) {
    fprintf(stderr, "Error initializing stream parameters: %s\n",
            av_err2str(res));
    return res;
  }

  ctx_internal->packet = av_packet_alloc();
  if (!ctx_internal->packet) {
    fprintf(stderr, "Error allocating video packet\n");
    return res;
  }

  ctx_internal->frame = av_frame_alloc();
  if (!ctx_internal->frame) {
    fprintf(stderr, "Error allocating video frame\n");
    return res;
  }

  ctx_internal->image_buffer_size = buffer_size;
  ctx_internal->image_buffer = malloc(ctx_internal->image_buffer_size);
  if (ctx_internal->image_buffer == NULL) {
    fprintf(stderr, "Error allocating image buffer: %s\n", strerror(errno));
    return -errno;
  }

  //
  // Start writing video feed output.
  //

  res = avformat_write_header(ctx_internal->output_format_ctx, NULL);
  if (res < 0) {
    fprintf(stderr, "Error writing output header: %s\n", av_err2str(res));
    return res;
  }

  // Loop forever or until error.
  for (int frame_i = 0; res >= 0; frame_i++) {
    size_t image_size =
        callbacks->fill_image_buffer(callbacks->callback_ctx,
                                     ctx_internal->image_buffer,
                                     ctx_internal->image_buffer_size);
    if (image_size < 0) {
      fprintf(stderr, "Error getting frame\n");
      return res;
    }

    res = create_video_frame(ctx_internal, image_size);
    if (res < 0) {
      fprintf(stderr, "Error decoding image frame %d\n", frame_i);
      return res;
    }

    ctx_internal->frame->pts = frame_i;
    res = send_video_frame(ctx_internal, 0 /* is_flush */);
    if (res < 0) {
      fprintf(stderr, "Error sending video frame %d\n", frame_i);
      return res;
    }

    usleep(1000 * 1000 / fps);  // Wait for the next frame tick.
  }

  // If the loop ends without error (unexpected), clean up the stream with a
  // null frame and trailer section.

  res = send_video_frame(ctx_internal, 1 /* is_flush */);
  if (res < 0) {
    fprintf(stderr, "Error sending flush frame\n");
    return res;
  }

  res = av_write_trailer(ctx_internal->output_format_ctx);
  if (res < 0) {
    fprintf(stderr, "Error writing output trailer: %s\n", av_err2str(res));
    return res;
  }

  callbacks->on_client_change(callbacks->callback_ctx, 0);

  return 0;
}
