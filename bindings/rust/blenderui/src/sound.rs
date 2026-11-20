//! A sound: an audio file or url on the default output, with no element and no
//! picture (`BK_sound_*`, kit/intern/audio/sound.c). It is a `<video>`'s sound
//! without the frames: the same OS readers (Media Foundation, AVFoundation,
//! GStreamer) decode it on the output's own thread, so no call here waits on a
//! decoder, a disk or the network.
//!
//! ```ignore
//! let song = Sound::new("song.m4a").unwrap(); // autoplays
//! song.set_volume(0.5);
//! if song.state() == VideoState::Ended { /* the next one */ }
//! ```

use std::ffi::CString;
use std::ptr::NonNull;
use std::time::Duration;

use crate::sys;
use crate::video::VideoState;

/// A playing (or paused) sound. UI thread only (`!Send`). It has no change
/// listener: read [`Sound::state`] when you redraw (a sound that played to its
/// end reports [`VideoState::Ended`]).
pub struct Sound {
    ptr: NonNull<sys::BKSound>,
}

impl Sound {
    /// Opens `src` and plays it. `None` for an empty path or one with a NUL
    /// byte, or when this build has no sound backend or output device. A file
    /// the system cannot decode reports [`VideoState::Error`] once tried.
    pub fn new(src: &str) -> Option<Self> {
        let path = CString::new(src).ok()?;
        // SAFETY: `path` is NUL-terminated for the call (the kit copies it).
        let ptr = NonNull::new(unsafe { sys::BK_sound_new(path.as_ptr()) })?;
        Some(Self { ptr })
    }

    const fn as_ptr(&self) -> *mut sys::BKSound {
        self.ptr.as_ptr()
    }

    /// Plays on; an ended sound starts over.
    pub fn play(&self) {
        // SAFETY: a live sound (all methods below likewise).
        unsafe { sys::BK_sound_play(self.as_ptr()) };
    }

    pub fn pause(&self) {
        unsafe { sys::BK_sound_pause(self.as_ptr()) };
    }

    /// Jumps to `t` (clamped to the duration when it is known).
    pub fn seek(&self, t: Duration) {
        unsafe { sys::BK_sound_seek(self.as_ptr(), t.as_secs_f64()) };
    }

    /// The volume, 0.0 ..= 1.0.
    pub fn set_volume(&self, volume: f64) {
        unsafe { sys::BK_sound_set_volume(self.as_ptr(), volume) };
    }

    pub fn volume(&self) -> f64 {
        unsafe { sys::BK_sound_volume(self.as_ptr()) }
    }

    /// The states of a video (loading, playing, paused, ended, error), and
    /// [`VideoState::NoOutput`] while there is no output to play to.
    pub fn state(&self) -> VideoState {
        VideoState::from_raw(unsafe { sys::BK_sound_state(self.as_ptr()) })
    }

    /// Where it is.
    pub fn time(&self) -> Duration {
        // a backend's NaN or infinity (an endless stream) is not a time: never panic on it
        Duration::try_from_secs_f64(unsafe { sys::BK_sound_time(self.as_ptr()) }.max(0.0)).unwrap_or_default()
    }

    /// `None` until opened (or when the backend can't tell).
    pub fn duration(&self) -> Option<Duration> {
        let d = unsafe { sys::BK_sound_duration(self.as_ptr()) };
        Duration::try_from_secs_f64(d).ok().filter(|d| !d.is_zero())
    }
}

impl Drop for Sound {
    fn drop(&mut self) {
        // SAFETY: ours; it returns at once (the output closes on its own thread).
        unsafe { sys::BK_sound_free(self.as_ptr()) };
    }
}

impl std::fmt::Debug for Sound {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Sound").field("state", &self.state()).finish_non_exhaustive()
    }
}
