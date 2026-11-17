//! A JavaScript app on Android: `assets/main.js` (one bundle: `bui bundle app.jsx -o main.js`, or
//! `build.py --js app.jsx`) runs on QuickJS with `@blender-ui/native`, React, Vue or Svelte inside it,
//! exactly as `bui build` runs it on a desktop.

use std::ffi::{c_char, c_int, c_void};

#[link(name = "android")]
unsafe extern "C" {
    fn AAssetManager_open(manager: *mut c_void, name: *const c_char, mode: c_int) -> *mut c_void;
    fn AAsset_getLength64(asset: *mut c_void) -> i64;
    fn AAsset_read(asset: *mut c_void, buf: *mut c_void, count: usize) -> c_int;
    fn AAsset_close(asset: *mut c_void);
}

unsafe extern "C" {
    /// The kit's handle on the APK's assets (an `AAssetManager *`).
    fn BK_android_assets() -> *mut c_void;
}

/// The bytes of `assets/<name>`.
fn asset(name: &std::ffi::CStr) -> Option<Vec<u8>> {
    // SAFETY: the manager lives as long as the activity; the asset is read whole and closed on
    // every path after it opened.
    unsafe {
        let manager = BK_android_assets(); // null before an app is attached
        if manager.is_null() {
            return None;
        }
        let a = AAssetManager_open(manager, name.as_ptr(), 3 /* AASSET_MODE_BUFFER */);
        if a.is_null() {
            return None;
        }
        let Ok(len) = usize::try_from(AAsset_getLength64(a)) else {
            AAsset_close(a);
            return None;
        };
        let mut bytes = vec![0u8; len];
        let read = AAsset_read(a, bytes.as_mut_ptr().cast(), bytes.len());
        AAsset_close(a);
        (usize::try_from(read) == Ok(len)).then_some(bytes)
    }
}

/// The app: runs `assets/main.js` until its windows are closed.
#[unsafe(no_mangle)]
pub extern "C" fn bui_app_main() {
    match asset(c"main.js") {
        Some(js) => blenderui_qjs::run("main.js".into(), js, Vec::new(), false),
        None => eprintln!("the APK has no assets/main.js"),
    }
}
