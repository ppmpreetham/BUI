//! `Sound`: an audio track plays with no window and no element, pauses, seeks,
//! ends, and a file without sound reports an error. Skips where this build or
//! machine has no backend or output device (CI without audio), and only then.

use std::sync::Mutex;
use std::time::{Duration, Instant};

use blenderui::{Sound, Video, VideoState};

/// The 2 s test clip with a 440 Hz tone.
fn clip() -> String {
    format!("{}/../../../tests/videos/clip_audio.mp4", env!("CARGO_MANIFEST_DIR")).replace('\\', "/")
}

/// The sound API is the UI thread's: the tests (threads of their own) take
/// turns, from opening a sound until it is freed.
static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());

/// Polls `sound` until `done` holds or `limit` passes; the last state.
fn wait(sound: &Sound, limit: Duration, done: impl Fn(VideoState) -> bool) -> VideoState {
    let start = Instant::now();
    loop {
        let state = sound.state();
        if done(state) || start.elapsed() > limit {
            return state;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

#[test]
fn sound_plays_pauses_seeks_and_ends() {
    let _turn = ONE_AT_A_TIME.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
    let Some(sound) = (!Video::backends().is_empty()).then(|| Sound::new(&clip())).flatten() else {
        eprintln!("no sound backend or output device: skipped");
        return;
    };
    sound.set_volume(0.0);
    assert!(sound.volume().abs() < 1e-9, "the volume is kept");
    let state = wait(&sound, Duration::from_secs(5), |s| s != VideoState::Loading);
    if state == VideoState::NoOutput {
        eprintln!("no output to play to: skipped");
        return;
    }
    assert_eq!(state, VideoState::Playing, "the clip plays (a decoder error, or still loading after 5 s, fails)");
    let duration = sound.duration().expect("the backend tells the length");
    assert!((duration.as_secs_f64() - 2.0).abs() < 0.2, "the clip is 2 s: {duration:?}");

    sound.pause();
    assert_eq!(sound.state(), VideoState::Paused);
    let held = sound.time();
    std::thread::sleep(Duration::from_millis(200));
    assert_eq!(sound.time(), held, "a paused sound holds its time");

    sound.seek(Duration::from_millis(1500));
    assert_eq!(sound.time(), Duration::from_millis(1500), "a seek shows its time at once");
    sound.play();
    assert_eq!(wait(&sound, Duration::from_secs(5), |s| s == VideoState::Ended), VideoState::Ended);
    assert_eq!(sound.time(), duration, "an ended sound is at its end");

    sound.play();
    assert_ne!(sound.state(), VideoState::Ended, "an ended sound starts over");
}

#[test]
fn a_file_without_sound_is_an_error() {
    let _turn = ONE_AT_A_TIME.lock().unwrap_or_else(std::sync::PoisonError::into_inner);
    let silent = format!("{}/../../../tests/videos/clip.mp4", env!("CARGO_MANIFEST_DIR")).replace('\\', "/");
    let Some(sound) = Sound::new(&silent) else {
        eprintln!("no sound backend or output device: skipped");
        return;
    };
    let state = wait(&sound, Duration::from_secs(5), |s| s != VideoState::Loading);
    if state == VideoState::NoOutput {
        eprintln!("no output, so the decoder never opened the file: skipped");
        return;
    }
    assert_eq!(state, VideoState::Error);
    assert!(Sound::new("").is_none(), "an empty path opens nothing");
}
