#include "bambucam.h"
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <stdarg.h>
#include <unistd.h>

#define URL_MAX_SIZE 2048
#define URL_OUTPUT_FORMAT "rtp://localhost:%s"

static void encode(AVCodecContext* enc_ctx, AVFrame* frame, AVPacket* pkt,
                   AVFormatContext* av_format_ctx, AVStream* av_stream) {
  int ret;

  /* send the frame to the encoder */
  if (frame)
    printf("Send frame %3"PRId64"\n", frame->pts);

  ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) {
    fprintf(stderr, "Error sending a frame for encoding\n");
    exit(1);
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return;
    else if (ret < 0) {
      fprintf(stderr, "Error during encoding\n");
      exit(1);
    }

    printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
    av_packet_rescale_ts(pkt, enc_ctx->time_base, av_stream->time_base);
    ret = av_write_frame(av_format_ctx, pkt);
    if (ret < 0) {
      printf("Error writing frame packet: %s\n", av_err2str(ret));
      return;
    }
    av_packet_unref(pkt);
  }
}

int main(int argc, char** argv) {
  if (argc != 5) {
    printf("Usage: %s <ip> <device> <passcode> <rtp-port>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  char* rtp_port = argv[4];
  char out_url[URL_MAX_SIZE];

  const AVCodec* av_encoder_codec = NULL;
  const AVCodec* av_decoder_codec = NULL;
  AVCodecContext* av_encoder_ctx = NULL;
  AVCodecContext* av_decoder_ctx = NULL;
  AVCodecParserContext* av_parser = NULL;
  AVIOContext* av_output_ctx = NULL;
  AVFormatContext* av_output_format_ctx = NULL;
  AVStream* av_output_stream = NULL;
  AVPacket* av_packet = NULL;
  AVFrame* av_frame = NULL;

  bambucam_ctx_t bambucam_ctx = NULL;

  int res = -1;
  uint8_t* frame_buffer = NULL;
  size_t frame_buffer_size = 0;

  av_log_set_level(AV_LOG_DEBUG);

  res = snprintf(out_url, URL_MAX_SIZE, URL_OUTPUT_FORMAT, rtp_port);
  if (res < 0) {
    printf("Error formatting output URL\n");
    goto close_and_exit;
  }

  res = avio_open(&av_output_ctx, out_url, AVIO_FLAG_WRITE);
  if (res < 0) {
    printf("Error opening output context %s: %s\n", out_url, av_err2str(res));
    goto close_and_exit;
  }

  av_output_format_ctx = avformat_alloc_context();
  if (!av_output_format_ctx) {
    printf("Error allocating output format context\n");
    res = -1;
    goto close_and_exit;
  }
  av_output_format_ctx->pb = av_output_ctx;

  av_output_format_ctx->oformat = av_guess_format("rtp_mpegts", NULL, NULL);
  if (!av_output_format_ctx->oformat) {
    printf("Error guessing output format: %s\n", out_url);
    res = -1;
    goto close_and_exit;
  }

  av_output_format_ctx->url = av_strdup(out_url);
  if (!av_output_format_ctx->oformat) {
    printf("Error allocating URL: %s\n", out_url);
    res = -1;
    goto close_and_exit;
  }

  av_encoder_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
  if (!av_encoder_codec) {
    printf("Encoder codec not found\n");
    res = -1;
    goto close_and_exit;
  }

  av_decoder_codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
  if (!av_encoder_codec) {
    printf("Decoder codec not found\n");
    res = -1;
    goto close_and_exit;
  }

  av_output_stream = avformat_new_stream(av_output_format_ctx, NULL);
  if (!av_output_stream) {
    printf("Error creating output stream\n");
    res = -1;
    goto close_and_exit;
  }

  av_encoder_ctx = avcodec_alloc_context3(av_encoder_codec);
  if (!av_encoder_ctx) {
    printf("Error allocating encoder codec context\n");
    res = -1;
    goto close_and_exit;
  }

  av_decoder_ctx = avcodec_alloc_context3(av_decoder_codec);
  if (!av_decoder_ctx) {
    printf("Error allocating decoder codec context\n");
    res = -1;
    goto close_and_exit;
  }

  av_parser = av_parser_init(av_decoder_codec->id);
  if (!av_parser) {
    printf("Error initializing decoder parser context\n");
    res = -1;
    goto close_and_exit;
  }

  res = bambucam_alloc_ctx(&bambucam_ctx);
  if (res < 0) {
    printf("Error allocation bambucam\n");
    goto close_and_exit;
  }

  res = bambucam_connect(bambucam_ctx, ip, device, passcode);
  if (res < 0) {
    printf("Error connecting via bambucam\n");
    goto close_and_exit;
  }

  frame_buffer_size = bambucam_get_max_frame_buffer_size(bambucam_ctx);
  frame_buffer = malloc(frame_buffer_size);
  if (frame_buffer == NULL) {
    printf("Error allocating frame buffer\n");
    goto close_and_exit;
  }

  int fps = bambucam_get_framerate(bambucam_ctx);
  av_channel_layout_default(&av_encoder_ctx->ch_layout, 1);
  av_encoder_ctx->width = bambucam_get_frame_width(bambucam_ctx);
  av_encoder_ctx->height = bambucam_get_frame_height(bambucam_ctx);
  av_encoder_ctx->bit_rate = av_encoder_ctx->width * av_encoder_ctx->height * 4;
  av_encoder_ctx->time_base = (AVRational) { 1, fps };
  av_encoder_ctx->framerate = (AVRational) { fps, 1 };
  av_encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

  if (av_output_format_ctx->flags & AVFMT_GLOBALHEADER)
    av_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  av_output_stream->time_base = av_encoder_ctx->time_base;

  res = avcodec_open2(av_encoder_ctx, av_encoder_codec, NULL);
  if (res < 0) {
    printf("Error opening encoder codec: %s\n", av_err2str(res));
    goto close_and_exit;
  }

  res = avcodec_open2(av_decoder_ctx, av_decoder_codec, NULL);
  if (res < 0) {
    printf("Error opening decoder codec: %s\n", av_err2str(res));
    goto close_and_exit;
  }

  res = avcodec_parameters_from_context(av_output_stream->codecpar,
                                        av_encoder_ctx);
  if (res < 0) {
    printf("Error initializing stream parameters: %s\n", av_err2str(res));
    goto close_and_exit;
  }

  av_packet = av_packet_alloc();
  if (!av_packet) {
    printf("Error allocating video packet\n");
    res = -1;
    goto close_and_exit;
  }

  av_frame = av_frame_alloc();
  if (!av_frame) {
    printf("Error allocating video frame\n");
    res = -1;
    goto close_and_exit;
  }

  res = avformat_write_header(av_output_format_ctx, NULL);
  if (res < 0) {
    printf("Error writing output header: %s\n", av_err2str(res));
    goto close_and_exit;
  }

  for (int frame_i = 0; res >= 0; frame_i++) {  // Loop forever.
    res = bambucam_get_frame(bambucam_ctx, frame_buffer, frame_buffer_size);
    if (res < 0) {
      printf("Error getting frame\n");
      goto close_and_exit;
    }

    const uint8_t* buffer = frame_buffer;
    int buffer_size = frame_buffer_size;
    while (buffer_size > 0) {
      res = av_parser_parse2(av_parser, av_decoder_ctx, &av_packet->data,
                             &av_packet->size, buffer, buffer_size,
                             AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (res < 0) {
        printf("Error creating JPEG packet: %s\n", av_err2str(res));
        goto close_and_exit;
      }
      buffer += res;
      buffer_size -= res;

      if (av_packet->size == 0) {
        continue;
      }

      res = avcodec_send_packet(av_decoder_ctx, av_packet);
      if (res < 0) {
        printf("Error sending JPEG packet: %s\n", av_err2str(res));
        goto close_and_exit;
      }

      res = avcodec_receive_frame(av_decoder_ctx, av_frame);
      if (res < 0 && res != AVERROR_EOF) {
        printf("Error receiving decoded JPEG frame: %s\n", av_err2str(res));
        goto close_and_exit;
      }
      av_packet_unref(av_packet);

      av_frame->pts = frame_i;
      encode(av_encoder_ctx, av_frame, av_packet, av_output_format_ctx,
             av_output_stream);
      av_frame_unref(av_frame);
    }

    usleep(1000 * 1000 / fps);
  }

  encode(av_encoder_ctx, NULL, av_packet, av_output_format_ctx,
         av_output_stream);

  res = av_write_trailer(av_output_format_ctx);
  if (res < 0) {
    printf("Error writing output trailer: %s\n", av_err2str(res));
    goto close_and_exit;
  }

close_and_exit:
  if (av_output_ctx) avio_closep(&av_output_ctx);
  if (av_output_format_ctx) avformat_free_context(av_output_format_ctx);
  if (av_parser) av_parser_close(av_parser);
  if (av_encoder_ctx) avcodec_free_context(&av_encoder_ctx);
  if (av_decoder_ctx) avcodec_free_context(&av_decoder_ctx);
  if (av_frame) av_frame_free(&av_frame);
  if (av_packet) av_packet_free(&av_packet);
  if (frame_buffer) free(frame_buffer);
  if (bambucam_ctx) bambucam_free_ctx(bambucam_ctx);
  return res;
}
