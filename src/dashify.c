/*
 * Parts of this file comes from libavformat/dashenc.c in the ffmpeg
 * source. Those portions are covered by the following license
 *
 * MPEG-DASH ISO BMFF segmenter
 * Copyright (c) 2014 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Parts of this file comes from doc/examples/remuxing.c in the ffmpeg
 * source. Those portions are covered by the following license
 *
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/avconfig.h>
#include <libavutil/intreadwrite.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

typedef struct AVCodecTag {
  enum AVCodecID id;
  unsigned int tag;
} AVCodecTag;

extern const AVCodecTag ff_mp4_obj_type[];

typedef struct OutputStream {
  AVFormatContext *ctx;
  int ctx_inited;
  uint8_t iobuf[32768];
  int packets_written;
  char initfile[1024];
  FILE * out;
  int64_t init_start_pos;
  int init_range_length;
  int nb_segments, segments_size, segment_index;
  int64_t first_pts, start_pts, max_pts;
  int64_t last_dts;
  int bit_rate;
  int fragment_index;
  char bandwidth_str[64];

  char codec_str[100];
} OutputStream;

/* From libavformat/dashenc.c in ffmpeg */

// RFC 6381
static void set_codec_str(AVFormatContext *s, AVCodecContext *codec,
                          char *str, int size)
{
    const AVCodecTag *tags[2] = { NULL, NULL };
    uint32_t tag;
    if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
        tags[0] = avformat_get_mov_video_tags();
    else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
        tags[0] = avformat_get_mov_audio_tags();
    else
        return;

    tag = av_codec_get_tag(tags, codec->codec_id);
    if (!tag)
        return;
    if (size < 5)
        return;

    AV_WL32(str, tag);
    str[4] = '\0';
    if (!strcmp(str, "mp4a") || !strcmp(str, "mp4v")) {
        uint32_t oti;
        tags[0] = ff_mp4_obj_type;
        oti = av_codec_get_tag(tags, codec->codec_id);
        if (oti)
            av_strlcatf(str, size, ".%02x", oti);
        else
            return;

        if (tag == MKTAG('m', 'p', '4', 'a')) {
            if (codec->extradata_size >= 2) {
                int aot = codec->extradata[0] >> 3;
                if (aot == 31)
                    aot = ((AV_RB16(codec->extradata) >> 5) & 0x3f) + 32;
                av_strlcatf(str, size, ".%d", aot);
            }
        } else if (tag == MKTAG('m', 'p', '4', 'v')) {
            // Unimplemented, should output ProfileLevelIndication as a decimal number
            av_log(s, AV_LOG_WARNING, "Incomplete RFC 6381 codec string for mp4v\n");
        }
    } else if (!strcmp(str, "avc1")) {
        uint8_t *tmpbuf = NULL;
        uint8_t *extradata = codec->extradata;
        int extradata_size = codec->extradata_size;
        if (!extradata_size)
            return;
        if (extradata[0] != 1) {
            AVIOContext *pb;
            if (avio_open_dyn_buf(&pb) < 0)
                return;
            if (ff_isom_write_avcc(pb, extradata, extradata_size) < 0) {
                ffio_free_dyn_buf(&pb);
                return;
            }
            extradata_size = avio_close_dyn_buf(pb, &extradata);
            tmpbuf = extradata;
        }

        if (extradata_size >= 4)
            av_strlcatf(str, size, ".%02x%02x%02x",
                        extradata[1], extradata[2], extradata[3]);
        av_free(tmpbuf);
    }
}

static int d_write(void *opaque, uint8_t *buf, int buf_size)
{
  int ret = 0;
  OutputStream *os = opaque;

  if (os->out) {
    ret = fwrite(buf, buf_size, 1, os->out);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Failed to write: %s", strerror(errno));
    }
  }
  return ret;
}


static int dash_write_header(AVStream *is, OutputStream *os)
{
    int ret = 0;
    AVOutputFormat *oformat;
    AVFormatContext *ctx;
    AVStream *st;
    AVDictionary *opts = NULL;
    int64_t timescale = av_rescale_q(1, (AVRational){1, 1}, is->time_base);

    oformat = av_guess_format("mp4", NULL, NULL);
    oformat->flags &= ~AVFMT_GLOBALHEADER;
    if (!oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    os->bit_rate = is->codec->bit_rate ?
      is->codec->bit_rate :
      is->codec->rc_max_rate;
    if (os->bit_rate) {
      snprintf(os->bandwidth_str, sizeof(os->bandwidth_str),
               " bandwidth=\"%d\"", os->bit_rate);
    } else {
      av_log(NULL, AV_LOG_WARNING, "No bit rate set for stream\n");
    }
    ctx = avformat_alloc_context();
    if (!ctx) {
      ret = AVERROR(ENOMEM);
      goto fail;
    }
    os->ctx = ctx;
    ctx->oformat = oformat;

    if (!(st = avformat_new_stream(ctx, NULL))) {
      ret = AVERROR(ENOMEM);
      goto fail;
    }
    avcodec_copy_context(st->codec, is->codec);
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      st->codec->codec_tag = av_codec_get_tag(oformat->codec_tag, AV_CODEC_ID_H264);
    st->sample_aspect_ratio = is->codec->sample_aspect_ratio;

    st->time_base = is->time_base;
    st->avg_frame_rate = is->avg_frame_rate;
    st->r_frame_rate = is->r_frame_rate;
    st->disposition = is->disposition;

    ctx->avoid_negative_ts = -1;
    ctx->pb = avio_alloc_context(os->iobuf, sizeof(os->iobuf), AVIO_FLAG_WRITE, os, NULL, d_write, NULL);
    ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if (!ctx->pb) {
      ret = AVERROR(ENOMEM);
      goto fail;
    }

    snprintf(os->initfile, sizeof(os->initfile), "out2.m4s");

    snprintf(ctx->filename, sizeof(ctx->filename), "%s%s", "./", os->initfile);

    av_dict_set(&opts, "movflags", "dash+frag_custom+frag_discont+global_sidx", 0);
    av_dict_set_int(&opts, "fragment_index", os->fragment_index, 0);
    av_dict_set_int(&opts, "video_track_timescale", timescale, 0);
    if ((ret = avformat_write_header(ctx, &opts)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Failed to write header: %d\n", ret);
      goto fail;
    }

    os->ctx_inited = 1;
    av_write_frame(ctx, NULL);
    avio_flush(ctx->pb);
    av_dict_free(&opts);

fail:
    return ret;
}

static void write_styp(AVIOContext *pb)
{
    avio_wb32(pb, 24);
    avio_wl32(pb, MKTAG('s', 't', 'y', 'p'));
    avio_wl32(pb, MKTAG('m', 's', 'd', 'h'));
    avio_wb32(pb, 0); /* minor */
    avio_wl32(pb, MKTAG('m', 's', 'h', 'd'));
    avio_wl32(pb, MKTAG('m', 's', 'i', 'x'));
}

static int dash_flush(AVFormatContext *s, OutputStream * os, int segment_index)
{
    int ret = 0;

    //    write_styp(os->ctx->pb);

    av_write_frame(os->ctx, NULL);
    avio_flush(os->ctx->pb);
    os->packets_written = 0;

    return ret;
}

static int dash_write_packet(AVFormatContext *s, AVPacket *pkt, OutputStream * os)
{
    // Fill in a heuristic guess of the packet duration, if none is available.
    // The mp4 muxer will do something similar (for the last packet in a fragment)
    // if nothing is set (setting it for the other packets doesn't hurt).
    // By setting a nonzero duration here, we can be sure that the mp4 muxer won't
    // invoke its heuristic (this doesn't have to be identical to that algorithm),
    // so that we know the exact timestamps of fragments.
    if (!pkt->duration && os->last_dts != AV_NOPTS_VALUE)
        pkt->duration = pkt->dts - os->last_dts;
    os->last_dts = pkt->dts;

    // If forcing the stream to start at 0, the mp4 muxer will set the start
    // timestamps to 0. Do the same here, to avoid mismatches in duration/timestamps.
    if (os->first_pts == AV_NOPTS_VALUE &&
        s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO) {
        pkt->pts -= pkt->dts;
        pkt->dts  = 0;
    }

    if (os->first_pts == AV_NOPTS_VALUE)
        os->first_pts = pkt->pts;

    if (!os->packets_written) {
        // If we wrote a previous segment, adjust the start time of the segment
        // to the end of the previous one (which is the same as the mp4 muxer
        // does). This avoids gaps in the timeline.
        if (os->max_pts != AV_NOPTS_VALUE)
            os->start_pts = os->max_pts;
        else
            os->start_pts = pkt->pts;
    }
    if (os->max_pts == AV_NOPTS_VALUE)
        os->max_pts = pkt->pts + pkt->duration;
    else
        os->max_pts = FFMAX(os->max_pts, pkt->pts + pkt->duration);
    os->packets_written++;
    return av_write_frame(os->ctx, pkt);
}



/* From doc/examples/remuxing.c in ffmpeg */
static int read_frame(AVFormatContext *ifmt_ctx, AVPacket *pkt, int stream_index) {
  int ret = 0;
  while(1) {
    ret = av_read_frame(ifmt_ctx, pkt);
    if (ret < 0) {
      break;
    }

    // This isn't the stream you're looking for
    if (pkt->stream_index != stream_index) {
      continue;
    }

    break;
  }

  return ret;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
  AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

  av_log(NULL, AV_LOG_DEBUG, "%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d %s\n",
          tag,
          av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
          av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
          av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
          pkt->stream_index, (pkt->flags & AV_PKT_FLAG_KEY) ? "key" : "");
}

static int find_stream(AVFormatContext *ctx, const char *spec)
{
  int ret, i;
  for (i = 0; i < ctx->nb_streams; i++) {
    ret = avformat_match_stream_specifier(ctx, ctx->streams[i], spec);
    if (ret < 0) {
      av_log(ctx, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
      break;
    } else if (ret > 0) {
      return i;
    }
  }

  return -1;
}

int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    FILE * out_file;
    const char *in_filename, *out_filename;
    int video_stream_index;
    int ret, i;
    int64_t segment = 1;
    int64_t seekfirst_ms = 0;
    int64_t seekfirst;
    int64_t duration_ms = 1000;
    int64_t duration;
    int64_t seg_end_ms = 0;
    int64_t seg_end;
    int64_t first_pts = -1;
    static int write_header = 0;
    static int verbose = 0;
    static int print_codec = 0;
    char *stream_identifier = "v:0";
    enum AVMediaType stream_type = AVMEDIA_TYPE_UNKNOWN;
    int stream_number = 0;
    struct stat st;
    avformat_network_init();
    avcodec_register_all();
    int c;

      static struct option long_options[] =
	{
	  /* These options don’t set a flag.
	     We distinguish them by their indices. */
	  {"duration",     required_argument,       0, 'd'},
	  {"segment",      required_argument,       0, 's'},
          {"stream",       required_argument,       0, 't'},
	  /* These options set a flag. */
          {"codec",        no_argument,       &print_codec, 1},
	  {"init",         no_argument,       &write_header, 1},
	  {"verbose",      no_argument,       &verbose, 1},
	  {0, 0, 0, 0}
	};
    while (1) {
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "d:s:iv",
		       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
	break;

      switch (c)
	{
	case 0:
	  /* If this option set a flag, do nothing else now. */
	  if (long_options[option_index].flag != 0)
	    break;
	  printf ("option %s", long_options[option_index].name);
	  if (optarg)
	    printf (" with arg %s", optarg);
	  printf ("\n");
	  break;

	case 'd':
	  duration_ms = atoi(optarg);
	  break;

	case 'i':
	  write_header = 1;
	  break;

	case 's':
	  segment = atoi(optarg);
	  break;

        case 'v':
          verbose = 1;
          break;

        case 't':
          stream_identifier = optarg;
          break;

	case '?':
	  /* getopt_long already printed an error message. */
	  break;

	default:
	  abort ();
	}
    }

    if (verbose) {
      av_log_set_level(AV_LOG_DEBUG);
    }

    if (argc - optind != 2) {
      fprintf(stderr, "usage: %s [--stream streamid] [--duration duration] [--segment number] [--init] [--codec] input output\n"
              "\n"
              "This application extracts a single segment from a single channel suitable for playing in a DASH stream\n"
              "\n"
              "  --stream streamid\t\tThe stream identifier to extract on the form [av]:[n]. Defaults to v:0\n"
              "  --duration\t\t\tThe requested duration of the segment, in milliseconds\n"
              "  --segment number\t\tThe segment number. Will seek to the requested segment\n"
              "  --init\t\t\tExtract the init segment with the moov header\n"
              "  --codec\t\t\tPrint the codec string for the requested stream instead of the data\n"
              "\n", argv[0]);
      return 1;
    }

    if (stream_identifier[0] == 'a') {
      stream_type = AVMEDIA_TYPE_AUDIO;
    } else if (stream_identifier[0] == 'v') {
      stream_type = AVMEDIA_TYPE_VIDEO;
    }

    {
      char *ptr = strchr(stream_identifier, ':');
      if (ptr) {
        stream_number = atoi(ptr+1);
      }
    }

    in_filename  = argv[optind];
    out_filename = argv[optind+1];


    if (stat(out_filename, &st) != -1) {
      fprintf(stderr, "File already exists: %s\n", out_filename);
      exit(1);
    }

    avcodec_register_all();
    av_register_all();

    // Open input
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        goto end;
    }

    //    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    ret = find_stream(ifmt_ctx, stream_identifier);

    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find a stream with specifier %s in the input file\n", stream_identifier);
      goto end;
    }
    video_stream_index = ret;

    AVStream *in_stream = ifmt_ctx->streams[video_stream_index];
    OutputStream *out_stream = av_mallocz(sizeof(OutputStream));

    i = video_stream_index;

    seekfirst_ms = (segment-1) * duration_ms;
    seg_end_ms = segment * duration_ms;
    seekfirst = av_rescale_q(seekfirst_ms*AV_TIME_BASE/1000, AV_TIME_BASE_Q, ifmt_ctx->streams[video_stream_index]->time_base);
    seg_end = av_rescale_q(seg_end_ms*AV_TIME_BASE/1000, AV_TIME_BASE_Q, ifmt_ctx->streams[video_stream_index]->time_base);
    duration = av_rescale_q(duration_ms*AV_TIME_BASE/1000, AV_TIME_BASE_Q, ifmt_ctx->streams[video_stream_index]->time_base);
    
    
    av_log(NULL, AV_LOG_DEBUG, "timebase = %d:%d\n", ifmt_ctx->streams[video_stream_index]->time_base.num, ifmt_ctx->streams[video_stream_index]->time_base.den);
    av_log(NULL, AV_LOG_DEBUG, "seekfirst = %" PRId64 "\n", seekfirst);
    av_log(NULL, AV_LOG_DEBUG, "seekfirst_ms = %" PRId64 "\n", seekfirst_ms);
    av_log(NULL, AV_LOG_DEBUG, "seg_end = %" PRId64 "\n", seg_end);
    av_log(NULL, AV_LOG_DEBUG, "seg_end_ms = %" PRId64 "\n", seg_end_ms);
    av_log(NULL, AV_LOG_DEBUG, "duration = %" PRId64 "\n", duration);
    av_log(NULL, AV_LOG_DEBUG, "duration_ms = %" PRId64 "\n", duration_ms);

    if (strcmp(out_filename, "-") == 0) {
      out_file = stdout;
    } else {
      out_file = fopen(out_filename, "w+");
    }

    if (print_codec)  {
      char codec[64];
      set_codec_str(ifmt_ctx, in_stream->codec, codec, sizeof(codec));
      fprintf(out_file, "%s", codec);
      return 0;
    }
    if (write_header) {
      out_stream->out = out_file;
    } else {
      // If this is a data-segment then we should discard the header
      out_stream->out = NULL;
    }


    out_stream->fragment_index = FFMAX(segment-1, 1);
    ret = dash_write_header(in_stream, out_stream);
    if (ret < 0) {
      goto end;
    }

    if (write_header) {
      goto end;
    }

    else {
      if (seekfirst > 0) {
        ret = read_frame(ifmt_ctx, &pkt, video_stream_index);
        if (ret < 0) {
	  if (ret != AVERROR_EOF) {
	    av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
	  }
          goto end;
        }
        pkt.stream_index = 0;
        ret = dash_write_packet(out_stream->ctx, &pkt, out_stream);
        dash_flush(out_stream->ctx, out_stream, 0);
      }
    }

    out_stream->out = out_file;

    if (seekfirst)
      ret = avformat_seek_file(ifmt_ctx, video_stream_index, seekfirst, seekfirst, INT64_MAX, 0);

    av_log(NULL, AV_LOG_DEBUG, "ret from seek: %d\n", ret);
    i = 0;

    while (1) {
      ret = read_frame(ifmt_ctx, &pkt, video_stream_index);
      if (ret < 0) {
	if (ret != AVERROR_EOF) {
	  av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
	}
        break;
      }

      if (pkt.stream_index != video_stream_index) {
        continue;
      }

      if (first_pts == -1) {
        first_pts = pkt.pts;
      }
      av_log(NULL, AV_LOG_DEBUG, "Comparing %"PRId64" and %"PRId64"\n", pkt.pts - first_pts, duration);

      if ((((in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) &&
           pkt.flags & AV_PKT_FLAG_KEY) ||
           in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) &&
          av_compare_ts(pkt.pts, in_stream->time_base,
                        seg_end, in_stream->time_base) >= 0) {

        break;
      }

      log_packet(ifmt_ctx, &pkt, "in");

      pkt.stream_index = 0;

      log_packet(out_stream->ctx, &pkt, "out");
      ret = dash_write_packet(out_stream->ctx, &pkt, out_stream);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error muxing packet\n");
        break;
      }
      av_packet_unref(&pkt);
      i++;
    }

    dash_flush(out_stream->ctx, out_stream, segment);

end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
      av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
      return 1;
    }

    return 0;
}

