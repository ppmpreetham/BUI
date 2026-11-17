//! `bui update`: this program from the newest release (checked, with a progress bar), then the BUI
//! packages of the project you are in.

use std::fmt::Write as _;
use std::fs;
use std::io::{Cursor, Read, Write};
use std::path::Path;

use anyhow::{Context, Result, bail};
use sha2::{Digest, Sha256};

use crate::check::Check;
use crate::cli::UpdateArgs;
use crate::sys::{OS, Os, semver};
use crate::{doctor, project, registry, ui};

pub fn run(args: &UpdateArgs) -> Result<()> {
    ui::banner(false);
    ui::intro("bui update");
    let have = env!("CARGO_PKG_VERSION");
    match registry::release_tag() {
        None => ui::warn("Could not reach GitHub to look for a newer bui."),
        Some(tag) if semver(&tag) > semver(have) => {
            ui::info(format!("A newer bui: {have} → {}", ui::accent(tag.trim_start_matches('v'))));
            if args.check {
                ui::info(format!("Update with {}", ui::code("bui update")));
            } else if ui::confirm("Update bui now?", true)? {
                replace_self(&tag)?;
                ui::success(format!("bui is now {}", tag.trim_start_matches('v')));
            }
        }
        Some(_) => ui::success(format!("bui {have} is the newest")),
    }

    if args.cli_only {
        ui::outro("Done");
        return Ok(());
    }
    let Some(p) = project::find(Path::new(".")) else {
        ui::outro("Done (run it inside a project to update its packages too)");
        return Ok(());
    };
    let app = p.meta.stack.app();
    ui::info(format!("Project {} ({})", p.meta.name, app.label()));
    if args.check {
        for c in app.checks(&p.dir) {
            ui::info(doctor::line(&c));
        }
    } else {
        let steps = app.update(&p.dir);
        if steps.is_empty() {
            ui::success("Nothing to update in the project");
        } else if ui::confirm("Update the project's BUI packages to the newest?", true)? {
            for s in &steps {
                if s.work.perform(&s.title)? {
                    continue;
                }
                // the reason was shown; an optional step may fail, a required one leaves the project half updated
                if !s.optional {
                    bail!("{} failed: the project's packages are not fully updated (`bui doctor` shows what is left)", s.title);
                }
                ui::warn(format!("{} did not work; carrying on", s.title));
            }
            let outdated: Vec<Check> = app.checks(&p.dir).into_iter().filter(|c| !c.is_ok()).collect();
            for c in &outdated {
                ui::warn(doctor::line(c));
            }
        }
    }
    ui::outro(&format!("{} Done", ui::accent("✦")));
    Ok(())
}

/// What the release's archive for this machine is called.
fn asset() -> Result<(String, bool)> {
    let triple = match (std::env::consts::OS, std::env::consts::ARCH) {
        ("windows", "x86_64") => "x86_64-pc-windows-msvc",
        ("windows", "aarch64") => "aarch64-pc-windows-msvc",
        ("linux", "x86_64") => "x86_64-unknown-linux-gnu",
        ("linux", "aarch64") => "aarch64-unknown-linux-gnu",
        ("macos", "x86_64") => "x86_64-apple-darwin",
        ("macos", "aarch64") => "aarch64-apple-darwin",
        (os, arch) => bail!("there is no bui build for {os} on {arch}"),
    };
    let zip = OS == Os::Windows;
    Ok((format!("bui-{triple}.{}", if zip { "zip" } else { "tar.gz" }), zip))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().fold(String::new(), |mut s, b| {
        let _ = write!(s, "{b:02x}");
        s
    })
}

fn replace_self(tag: &str) -> Result<()> {
    let (asset, zip) = asset()?;
    let bar = ui::progress(&format!("Downloading {asset}"), 1);
    let archive = registry::download(&registry::download_url(tag, &asset), |done, total| bar.position(done, total));
    let Some(archive) = archive else {
        bar.fail("Download failed");
        bail!("could not download {asset} for {tag}");
    };
    bar.done(&format!("Downloaded {:.1} MB", archive.len() as f64 / 1_048_576.0));

    // the checksum the release publishes next to it
    let sums = registry::download(&registry::download_url(tag, &format!("{asset}.sha256")), |_, _| {})
        .and_then(|b| String::from_utf8(b).ok())
        .context("the release has no checksum for this download")?;
    let want = sums.split_whitespace().next().unwrap_or_default().to_ascii_lowercase();
    if hex(&Sha256::digest(&archive)) != want {
        bail!("the download does not match its checksum: not installing it");
    }
    ui::success("Checksum matches");

    let exe_name = if zip { "bui.exe" } else { "bui" };
    let binary = if zip {
        let mut z = zip::ZipArchive::new(Cursor::new(archive))?;
        let mut file = z.by_name(exe_name).with_context(|| format!("{exe_name} is not in the archive"))?;
        let mut out = Vec::new();
        file.read_to_end(&mut out)?;
        out
    } else {
        let mut tar = tar::Archive::new(flate2::read::GzDecoder::new(Cursor::new(archive)));
        let mut out = Vec::new();
        for entry in tar.entries()? {
            let mut entry = entry?;
            if entry.path()?.file_name().is_some_and(|n| n == exe_name) {
                entry.read_to_end(&mut out)?;
                break;
            }
        }
        if out.is_empty() {
            bail!("{exe_name} is not in the archive");
        }
        out
    };
    let staged = stage(&binary, zip)?;
    // the file that goes in is the one that was verified: read back, it is the same bytes
    if fs::read(&staged).ok().is_none_or(|b| Sha256::digest(&b) != Sha256::digest(&binary)) {
        bail!("the staged copy of the new bui changed before it could be installed: not installing it");
    }
    // `staged` deletes itself when it goes out of scope, installed or not
    self_replace::self_replace(&staged).context("replacing the running bui")
}

/// Writes `binary` to a new file with a random name that nothing else can have made (it must not exist) and
/// only this user can read or change, beside the running bui (the same disk, in a folder that is already
/// the user's) or else in the temp folder. The file is deleted when the returned path is dropped.
fn stage(binary: &[u8], zip: bool) -> Result<tempfile::TempPath> {
    let mut builder = tempfile::Builder::new();
    builder.prefix(".bui-update-").suffix(if zip { ".exe" } else { "" });
    let beside = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf));
    let mut file = match beside.map(|dir| builder.tempfile_in(dir)) {
        Some(Ok(file)) => file,
        _ => builder.tempfile().context("making a private file for the new bui")?, // 0600 on Unix, exclusive
    };
    file.write_all(binary)?;
    file.flush()?;
    let path = file.into_temp_path(); // closes the file; the path still deletes it
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700))?;
    }
    Ok(path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_staged_binary_is_a_new_file_that_goes_away() {
        let one = stage(b"one", false).unwrap();
        let two = stage(b"two", false).unwrap();
        assert_ne!(one.to_path_buf(), two.to_path_buf(), "names are random, never reused");
        assert_eq!(fs::read(&one).unwrap(), b"one");
        let path = one.to_path_buf();
        drop(one);
        assert!(!path.exists());
    }
}
