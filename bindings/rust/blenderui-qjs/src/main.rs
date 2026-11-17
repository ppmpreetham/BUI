//! `bui-runtime`'s executable: finds the app's JavaScript and runs it (`blenderui_qjs::run`).
//!
//!   bui-runtime app.js [args]       run a script (development)
//!   app.exe [args]                  an app built by `bui build`
//!   BUI_HEADLESS=1 / --headless     off-screen windows (tests)
//!
//! SPDX-License-Identifier: GPL-2.0-or-later

/// The end of an app executable: `MAGIC`, then the bundle's length (u64 LE).
const MAGIC: &[u8; 8] = b"BUIAPP01";

/// The app's JavaScript: the bundle appended to this executable, or the
/// script named on the command line. Returns (name, source, script args).
fn load() -> Result<(String, Vec<u8>, Vec<String>), String> {
    let mut args: Vec<String> = std::env::args().skip(1).filter(|a| a != "--headless").collect();
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    if let Ok(bytes) = std::fs::read(&exe) {
        if bytes.len() > 16 && &bytes[bytes.len() - 16..bytes.len() - 8] == MAGIC {
            let n = u64::from_le_bytes(bytes[bytes.len() - 8..].try_into().unwrap()) as usize;
            if n <= bytes.len() - 16 {
                let start = bytes.len() - 16 - n;
                let name = exe.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
                return Ok((name, bytes[start..bytes.len() - 16].to_vec(), args));
            }
        }
    }
    if args.is_empty() {
        return Err("usage: bui-runtime <app.js> [args]   (or build an app with `bui build`)".into());
    }
    let path = args.remove(0);
    let src = std::fs::read(&path).map_err(|e| format!("{path}: {e}"))?;
    Ok((path, src, args))
}

fn main() {
    let (name, source, args) = match load() {
        Ok(x) => x,
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(2);
        }
    };
    let headless = std::env::args().any(|a| a == "--headless") || std::env::var("BUI_HEADLESS").is_ok_and(|v| v == "1");
    blenderui_qjs::run(name, source, args, headless);
}
