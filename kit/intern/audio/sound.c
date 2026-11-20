/* BK_sound_*: an audio file (or url) played on the default output, with no picture:
 * what a BKVideo's sound does, without the frames.
 *
 * A thin layer over the player in audio.c. The video backend of this platform (Media
 * Foundation, AVFoundation, GStreamer) reads the first audio track on the device's thread,
 * so opening, decoding and seeking never wait on the UI thread. This file keeps only what
 * the UI asked for: playing or not, the volume, and the time to show while the sound is not
 * the clock (paused, just started, just seeked).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "MEM_guardedalloc.h"

#include "../image/image.h"
#include "audio.h"

struct BKSound {
  BKAudio *audio;
  double volume;
  double at; /* the time shown while the clock doesn't run */
  bool playing;
};

/* The first compiled backend that reads sound (one per platform). */
static const BKImAnimCodec *sound_codec(void)
{
  for (const BKImAnimCodec *const *c = imb_movie_codecs; *c; c++) {
    if ((*c)->audio_open) {
      return *c;
    }
  }
  return NULL;
}

BKSound *BK_sound_new(const char *src)
{
  BKAudio *audio = src && src[0] ? bk_audio_new(sound_codec(), src, 0) : NULL;
  if (!audio) {
    return NULL;
  }
  BKSound *s = (BKSound *)MEM_callocN(sizeof(BKSound), "BKSound");
  s->audio = audio;
  s->volume = 1.0;
  s->playing = true;
  bk_audio_play(audio, true);
  return s;
}

void BK_sound_free(BKSound *s)
{
  if (s) {
    bk_audio_free(s->audio); /* returns at once: the device winds down on its own thread */
    MEM_freeN(s);
  }
}

void BK_sound_play(BKSound *s)
{
  if (!s) {
    return;
  }
  if (bk_audio_ended(s->audio)) { /* an ended sound starts over, as a video does */
    s->at = 0.0;
    bk_audio_seek(s->audio, 0.0);
  }
  s->playing = true;
  bk_audio_play(s->audio, true);
}

void BK_sound_pause(BKSound *s)
{
  if (s && s->playing) {
    s->at = BK_sound_time(s);
    s->playing = false;
    bk_audio_play(s->audio, false);
  }
}

void BK_sound_seek(BKSound *s, double seconds)
{
  if (!s) {
    return;
  }
  const double duration = bk_audio_duration(s->audio);
  s->at = !(seconds > 0.0) ? 0.0 : duration > 0.0 && seconds > duration ? duration : seconds;
  bk_audio_seek(s->audio, s->at);
}

void BK_sound_set_volume(BKSound *s, double volume)
{
  if (s) {
    s->volume = bk_audio_clamp_gain((float)volume);
    bk_audio_set_gain(s->audio, (float)s->volume);
  }
}

double BK_sound_volume(const BKSound *s)
{
  return s ? s->volume : 0.0;
}

int BK_sound_state(const BKSound *s)
{
  if (!s || bk_audio_failed(s->audio)) {
    return BK_SOUND_ERROR;
  }
  if (bk_audio_ended(s->audio)) {
    return BK_SOUND_ENDED;
  }
  if (!s->playing) {
    return BK_SOUND_PAUSED;
  }
  if (bk_audio_waiting(s->audio)) {
    return BK_SOUND_NO_OUTPUT; /* it plays once an output is there */
  }
  return bk_audio_has_track(s->audio) ? BK_SOUND_PLAYING : BK_SOUND_LOADING;
}

double BK_sound_time(const BKSound *s)
{
  if (!s) {
    return 0.0;
  }
  if (bk_audio_ended(s->audio)) {
    const double duration = bk_audio_duration(s->audio);
    return duration > 0.0 ? duration : s->at;
  }
  const double clock = s->playing ? bk_audio_clock(s->audio) : -1.0;
  return clock >= 0.0 ? clock : s->at;
}

double BK_sound_duration(const BKSound *s)
{
  return s ? bk_audio_duration(s->audio) : 0.0;
}
