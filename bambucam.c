#include "bambu.h"
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <stdarg.h>
#include <unistd.h>

#define URL_MAX_SIZE 2048
#define URL_FORMAT "bambu:///local/%s.?port=6000&" \
                   "user=bblp&passwd=%s&device=%s&" \
                   "version=00.00.00.00"

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

static void bambu_log(void *ctx, int lvl, const char *msg) {
  printf("Bambu<%d>: %s\n", lvl, msg);
  Bambu_FreeLogMsg(msg);
}

int main(int argc, char** argv) {
  if (argc != 5) {
    printf("Usage: %s <ip> <device> <passcode> <output.mkv>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  char* out_filename = argv[4];
  char url[URL_MAX_SIZE];
  snprintf(url, URL_MAX_SIZE, URL_FORMAT, ip, passcode, device);

  const AVCodec* av_encoder_codec = NULL;
  const AVCodec* av_decoder_codec = NULL;
  AVCodecContext* av_encoder_ctx = NULL;
  AVCodecContext* av_decoder_ctx = NULL;
  AVCodecParserContext* parser = NULL;
  AVIOContext* av_output_ctx = NULL;
  AVFormatContext* av_output_format_ctx = NULL;
  AVStream* av_output_stream = NULL;
  AVPacket* av_packet = NULL;
  AVFrame* av_frame = NULL;

  Bambu_Tunnel tnl;
  Bambu_StreamInfo info;
  Bambu_Sample sample;

  int res = -1;

  av_log_set_level(AV_LOG_DEBUG);

  res = avio_open(&av_output_ctx, out_filename, AVIO_FLAG_WRITE);
  if (res < 0) {
    printf("Error opening output context %s: %s\n",
           out_filename, av_err2str(res));
    goto close_and_exit;
  }

  av_output_format_ctx = avformat_alloc_context();
  if (!av_output_format_ctx) {
    printf("Error allocating output format context\n");
    res = -1;
    goto close_and_exit;
  }
  av_output_format_ctx->pb = av_output_ctx;

  av_output_format_ctx->oformat = av_guess_format(NULL, out_filename, NULL);
  if (!av_output_format_ctx->oformat) {
    printf("Error guessing output format: %s\n", out_filename);
    res = -1;
    goto close_and_exit;
  }

  av_output_format_ctx->url = av_strdup(out_filename);
  if (!av_output_format_ctx->oformat) {
    printf("Error allocating URL: %s\n", out_filename);
    res = -1;
    goto close_and_exit;
  }

  av_encoder_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
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

  parser = av_parser_init(av_decoder_codec->id);
  if (!parser) {
    printf("Error initializing decoder parser context\n");
    res = -1;
    goto close_and_exit;
  }

  res = Bambu_Create(&tnl, url);
  if (res != Bambu_success) {
    printf("Error creating Bambu Tunnel: %d\n", res);
    goto close_and_exit;
  }

  Bambu_SetLogger(tnl, bambu_log, NULL);
  res = Bambu_Open(tnl);
  if (res != Bambu_success) {
    printf("Error opening Bambu Tunnel: %d\n", res);
    goto close_and_exit;
  }

  while ((res = Bambu_StartStream(tnl, 1 /* video */)) == Bambu_would_block) {
    usleep(100000);
  }
  if (res != Bambu_success) {
    printf("Error starting stream: %d\n", res);
    goto close_and_exit;
  }

  res = Bambu_GetStreamCount(tnl);
  if (res != 1) {
    printf("Expected one video stream, got %d\n", res);
    res = -1;
    goto close_and_exit;
  }

  Bambu_GetStreamInfo(tnl, 1, &info);
  if (info.type != VIDE) {
    printf("Expected stream type VIDE, got %d\n", info.type);
    res = -1;
    goto close_and_exit;
  }

  printf("Stream: type=%d, sub_type=%d width=%d height=%d, "
         "frame_rate=%d format_size=%d max_frame_size=%d\n",
         info.type, info.sub_type, info.format.video.width,
         info.format.video.height, info.format.video.frame_rate,
         info.format_size, info.max_frame_size);

  av_channel_layout_default(&av_encoder_ctx->ch_layout, 1);
  av_encoder_ctx->width = info.format.video.width;
  av_encoder_ctx->height = info.format.video.height;
  av_encoder_ctx->bit_rate = av_encoder_ctx->width * av_encoder_ctx->height * 4;
  av_encoder_ctx->time_base = (AVRational) { 1, info.format.video.frame_rate };
  av_encoder_ctx->framerate = (AVRational) { info.format.video.frame_rate, 1 };
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

  for (int i = 0; i < 30; i++) {
    while ((res = Bambu_ReadSample(tnl, &sample)) == Bambu_would_block) {
      usleep(33333); /* 30Hz */
    }
    if (res == Bambu_stream_end) {
      printf("End of stream, exiting");
      res = 0;
      goto close_and_exit;
    } else if (res != Bambu_success) {
      printf("End of stream, exiting");
      goto close_and_exit;
    }

    printf("Sample %d: size=%d flags=%d decode_time=%llu\n",
           i, sample.size, sample.flags, sample.decode_time);

    const uint8_t* buffer = sample.buffer;
    int buffer_size = sample.size;
    while (buffer_size > 0) {
      res = av_parser_parse2(parser, av_decoder_ctx, &av_packet->data,
                             &av_packet->size, sample.buffer, sample.size,
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

      av_frame->pts = i;
      encode(av_encoder_ctx, av_frame, av_packet, av_output_format_ctx,
             av_output_stream);
      av_frame_unref(av_frame);
    }

    usleep(1000 * 1000 / info.format.video.frame_rate);
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
  if (av_encoder_ctx) avcodec_free_context(&av_encoder_ctx);
  if (av_decoder_ctx) avcodec_free_context(&av_decoder_ctx);
  if (av_frame) av_frame_free(&av_frame);
  if (av_packet) av_packet_free(&av_packet);
  if (tnl) Bambu_Close(tnl);
  if (tnl) Bambu_Destroy(tnl);
  return res;
}
