// SpdifEncoder.cpp  — see SpdifEncoder.h for design notes.

#include "SpdifEncoder.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

namespace {

void LogAv(const char* what, int err)
{
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(err, buf, sizeof buf);
  std::fprintf(stderr, "[SpdifEncoder] %s failed: %s (%d)\n", what, buf, err);
}

} // namespace

SpdifEncoder::~SpdifEncoder()
{
  Close();
}

bool SpdifEncoder::Init(const Params& p)
{
  Close();

  av_channel_layout_copy(&inLayout_, &p.inLayout);
  inSampleFmt_ = p.inSampleFmt;
  nextPts_ = 0;

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
  if (!codec)
  {
    std::fprintf(stderr, "[SpdifEncoder] AC3 encoder not found in this FFmpeg build\n");
    return false;
  }

  codecCtx_ = avcodec_alloc_context3(codec);
  if (!codecCtx_)
    return false;

  codecCtx_->bit_rate = p.bitRate;
  codecCtx_->sample_rate = p.sampleRate;
  codecCtx_->sample_fmt = AV_SAMPLE_FMT_FLTP; // FFmpeg's AC3 encoder takes planar float
  codecCtx_->time_base = av_make_q(1, p.sampleRate);

  // The encoder (and S/PDIF) carry 5.1. Prefer the "back" variant to match Windows'
  // KSAUDIO_SPEAKER_5POINT1 (FL FR FC LFE BL BR); fall back to "side" 5.1 if the
  // encoder build rejects it.
  AVChannelLayout enc51back = AV_CHANNEL_LAYOUT_5POINT1_BACK;
  AVChannelLayout enc51side = AV_CHANNEL_LAYOUT_5POINT1;
  av_channel_layout_copy(&codecCtx_->ch_layout, &enc51back);

  int rc = avcodec_open2(codecCtx_, codec, nullptr);
  if (rc < 0)
  {
    av_channel_layout_uninit(&codecCtx_->ch_layout);
    av_channel_layout_copy(&codecCtx_->ch_layout, &enc51side);
    rc = avcodec_open2(codecCtx_, codec, nullptr);
    if (rc < 0) { LogAv("avcodec_open2", rc); return false; }
  }

  framesPerPacket_ = codecCtx_->frame_size; // 1536 for AC3

  // swr: interleaved input (any layout) -> planar-float 5.1 (the encoder layout).
  // Same sample rate in/out, so swr only converts sample format + channel layout
  // (a downmix when input has > 5.1 channels).
  rc = swr_alloc_set_opts2(&swr_,
                           &codecCtx_->ch_layout, AV_SAMPLE_FMT_FLTP, p.sampleRate,
                           &inLayout_, inSampleFmt_, p.sampleRate,
                           0, nullptr);
  if (rc < 0 || !swr_) { LogAv("swr_alloc_set_opts2", rc); return false; }
  rc = swr_init(swr_);
  if (rc < 0) { LogAv("swr_init", rc); return false; }

  // Scratch frame holding the converted planar samples handed to the encoder.
  frame_ = av_frame_alloc();
  if (!frame_) return false;
  frame_->format = AV_SAMPLE_FMT_FLTP;
  av_channel_layout_copy(&frame_->ch_layout, &codecCtx_->ch_layout);
  frame_->sample_rate = p.sampleRate;
  frame_->nb_samples = framesPerPacket_;
  rc = av_frame_get_buffer(frame_, 0);
  if (rc < 0) { LogAv("av_frame_get_buffer", rc); return false; }

  pkt_ = av_packet_alloc();
  if (!pkt_) return false;

  // S/PDIF muxer with a custom AVIO sink so each burst lands in the caller's buffer.
  rc = avformat_alloc_output_context2(&muxer_, nullptr, "spdif", nullptr);
  if (rc < 0 || !muxer_) { LogAv("avformat_alloc_output_context2(spdif)", rc); return false; }

  AVStream* st = avformat_new_stream(muxer_, nullptr);
  if (!st) { std::fprintf(stderr, "[SpdifEncoder] avformat_new_stream failed\n"); return false; }
  st->id = 0;
  st->time_base = codecCtx_->time_base;
  rc = avcodec_parameters_from_context(st->codecpar, codecCtx_);
  if (rc < 0) { LogAv("avcodec_parameters_from_context", rc); return false; }

  // The muxer never needs more than one burst at a time.
  unsigned char* avioBuf = static_cast<unsigned char*>(av_malloc(kMaxBytesPerPacket));
  if (!avioBuf) return false;
  muxer_->pb = avio_alloc_context(avioBuf, kMaxBytesPerPacket, /*write_flag=*/1, this,
                                  /*read=*/nullptr, &WritePacketThunk, /*seek=*/nullptr);
  if (!muxer_->pb) { av_free(avioBuf); return false; }
  muxer_->flags |= AVFMT_FLAG_CUSTOM_IO;

  rc = avformat_write_header(muxer_, nullptr);
  if (rc < 0) { LogAv("avformat_write_header(spdif)", rc); return false; }

  return true;
}

void SpdifEncoder::Close()
{
  if (muxer_)
  {
    if (muxer_->pb) // a header was written iff pb exists
      av_write_trailer(muxer_);
    if (muxer_->pb)
    {
      av_freep(&muxer_->pb->buffer);
      avio_context_free(&muxer_->pb);
    }
    avformat_free_context(muxer_);
    muxer_ = nullptr;
  }
  if (swr_)       swr_free(&swr_);
  if (frame_)     av_frame_free(&frame_);
  if (pkt_)       av_packet_free(&pkt_);
  if (codecCtx_)  avcodec_free_context(&codecCtx_);
  av_channel_layout_uninit(&inLayout_);
  framesPerPacket_ = 0;
  nextPts_ = 0;
}

int SpdifEncoder::EncodePacket(const uint8_t* in, uint8_t* outBuf, int outSize)
{
  if (!codecCtx_)
    return -1;
  if (outSize < kMaxBytesPerPacket)
    return -1;

  int rc = av_frame_make_writable(frame_);
  if (rc < 0) { LogAv("av_frame_make_writable", rc); return -1; }

  // Convert/downmix the interleaved input into the planar-float encoder frame.
  const uint8_t* inPlanes[1] = { in };
  int got = swr_convert(swr_, frame_->data, frame_->nb_samples, inPlanes, framesPerPacket_);
  if (got < 0) { LogAv("swr_convert", got); return -1; }

  frame_->pts = nextPts_;
  nextPts_ += framesPerPacket_;

  rc = avcodec_send_frame(codecCtx_, frame_);
  if (rc < 0) { LogAv("avcodec_send_frame", rc); return -1; }

  rc = avcodec_receive_packet(codecCtx_, pkt_);
  if (rc == AVERROR(EAGAIN))
    return 0; // encoder buffering; no burst this call (AC3 is normally 1-in/1-out)
  if (rc < 0) { LogAv("avcodec_receive_packet", rc); return -1; }

  pkt_->stream_index = 0;

  writeDst_ = outBuf;
  writeCap_ = outSize;
  writeLen_ = 0;

  rc = av_write_frame(muxer_, pkt_);
  av_packet_unref(pkt_);
  if (rc < 0) { LogAv("av_write_frame(spdif)", rc); return -1; }
  avio_flush(muxer_->pb); // force the muxer to emit the full burst through OnWritePacket

  int n = writeLen_;
  writeDst_ = nullptr;
  writeCap_ = 0;
  writeLen_ = 0;
  return n;
}

int SpdifEncoder::WritePacketThunk(void* opaque, const uint8_t* buf, int buf_size)
{
  return static_cast<SpdifEncoder*>(opaque)->OnWritePacket(buf, buf_size);
}

int SpdifEncoder::OnWritePacket(const uint8_t* buf, int buf_size)
{
  int n = buf_size;
  if (writeLen_ + n > writeCap_)
    n = writeCap_ - writeLen_;
  if (n > 0)
  {
    std::memcpy(writeDst_ + writeLen_, buf, static_cast<size_t>(n));
    writeLen_ += n;
  }
  return buf_size; // report full consumption to the muxer even if we clamped
}
