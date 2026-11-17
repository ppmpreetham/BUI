//! `bui new` and `bui init`: ask what to make, make sure its tools are there, say what will be installed
//! and ask, write the project, and install the newest BUI and tooling with the language's own
//! package manager.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

use anyhow::{Result, bail};

use crate::cli::{Choices, Lang, NewArgs};
use crate::flavours::{File, Step, Vars};
use crate::flavours::{c::CApp, js::JsApp, lua::LuaApp, python::PythonApp, rust::RustApp};
use crate::project::{FILE, Framework, Language, Meta, PackageManager, PyApi, Stack};
use crate::sys::{Cmd, which};
use crate::ui::{self, choice};
use crate::{registry, tools};

pub fn new(args: NewArgs) -> Result<()> {
    ui::banner(true);
    ui::intro("Let's make an app");
    let name = match args.name {
        Some(n) => n,
        None => ui::input("What do you want to call it?", "my-app", |s| bad_name(s.as_str()))?,
    };
    if let Some(problem) = bad_name(&name) {
        bail!("{problem}");
    }
    let dir = std::env::current_dir()?.join(&name);
    if dir.exists() && fs::read_dir(&dir)?.next().is_some() {
        bail!(
            "{} already exists and has files in it. Pick another name, or go into that folder and run `bui init`.",
            dir.display()
        );
    }
    make(&name, &dir, &args.choices, true)
}

pub fn init(choices: &Choices) -> Result<()> {
    ui::banner(true);
    ui::intro("Add a BUI app to this folder");
    let dir = std::env::current_dir()?;
    if dir.join(FILE).exists() {
        bail!("this folder is already a BUI app ({FILE} is here). Run `bui doctor` to check it.");
    }
    let raw = dir.file_name().map_or_else(|| "app".into(), |n| n.to_string_lossy().into_owned());
    make(&sanitize(&raw), &dir, choices, false)
}

/// A folder name as an app name: lowercase letters, digits, `-` and `_`.
fn sanitize(raw: &str) -> String {
    let s: String = raw.chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' { c.to_ascii_lowercase() } else { '-' }).collect();
    let s = s.trim_matches('-').to_owned();
    if s.is_empty() || s.starts_with(|c: char| c.is_ascii_digit()) { format!("app-{s}") } else { s }
}

fn bad_name(name: &str) -> Option<String> {
    let ok = !name.is_empty()
        && name.starts_with(|c: char| c.is_ascii_lowercase())
        && name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '_');
    (!ok).then(|| "Use lowercase letters, digits, - and _, starting with a letter (for example my-app).".to_owned())
}

fn make(name: &str, dir: &Path, choices: &Choices, new_folder: bool) -> Result<()> {
    let started = Instant::now();
    let stack = ask(choices)?;
    let app = stack.app();
    ui::info(format!("Setting up {}", ui::accent(&app.label())));

    // the tools the language needs, offered for install when missing
    let missing = tools::ensure(&app.tools())?;

    // what will be installed, said and asked before anything is written
    let steps = app.deps(dir);
    let install_now = if choices.no_install {
        false
    } else if !missing.is_empty() {
        ui::warn("I can't install the packages until those tools are there. `bui doctor` will do it once they are.");
        false
    } else {
        ui::note("I'll install the newest versions of", &step_list(&steps));
        ui::confirm("Install them now?", true)?
    };

    // the files that are there already, made to fit (a manifest that cannot be read stops here, with nothing written)
    let adopted = app.adopt(dir)?;
    let writing = ui::task("Writing your app");
    fs::create_dir_all(dir)?;
    let mut files = app.files(&Vars::new(name, &release_tag()));
    files.push(File { path: FILE.into(), body: Meta { name: name.to_owned(), stack: stack.clone() }.to_toml()? });
    let (wrote, mut skipped) = write_files(dir, files)?;
    skipped.retain(|f| !adopted.iter().any(|a| a.path == f));
    writing.done(&format!("Wrote {wrote} files"));
    for a in &adopted {
        ui::success(format!("{}: {}", a.path, a.note));
    }
    if !skipped.is_empty() {
        ui::warn(format!("Left your existing {} as it was", skipped.join(", ")));
    }

    git_init(dir, choices.no_git)?;

    if !(install_now && install(&steps)?) {
        ui::info(format!("Install the packages later with {}", ui::code("bui doctor")));
    }

    ui::note("What's next", &next_steps(name, new_folder));
    ui::outro(&format!("{} {name} is ready {}", ui::accent("✦"), ui::dim(&format!("({})", took(started)))));
    Ok(())
}

/// The newest BUI release, as a git tag (for the C template; the others install through their package manager).
fn release_tag() -> String {
    registry::bui_version().map_or_else(|| format!("v{}", env!("CARGO_PKG_VERSION")), |v| format!("v{v}"))
}

/// The install steps with the command each runs.
fn step_list(steps: &[Step]) -> String {
    steps.iter().map(|s| format!("{}\n    $ {}", s.title, ui::code(&s.work.display()))).collect::<Vec<_>>().join("\n")
}

/// Every command, so there is nothing to look up afterwards.
fn next_steps(name: &str, new_folder: bool) -> String {
    let mut lines = Vec::new();
    if new_folder {
        lines.push(format!("$ {}", ui::code(&format!("cd {name}"))));
    }
    for (cmd, what) in [
        ("bui dev", "run it (reloads when you save)"),
        ("bui build", "make a release build"),
        ("bui doctor", "check your setup and fix what is wrong"),
        ("bui update", "get newer versions"),
        ("bui docs", "open the documentation"),
    ] {
        lines.push(format!("$ {}  {}", ui::code(&format!("{cmd:<11}")), ui::dim(what)));
    }
    lines.push(String::new());
    lines.push(ui::dim("Every command takes --help for its options."));
    lines.join("\n")
}

fn took(started: Instant) -> String {
    let s = started.elapsed().as_secs();
    if s < 60 { format!("took {s}s") } else { format!("took {}m {}s", s / 60, s % 60) }
}

/// Writes the files that are not there yet; returns how many it wrote and which it left alone.
fn write_files(dir: &Path, files: Vec<File>) -> Result<(usize, Vec<String>)> {
    let mut wrote = 0;
    let mut skipped = Vec::new();
    for f in files {
        let path: PathBuf = dir.join(&f.path);
        if path.exists() {
            skipped.push(f.path);
            continue;
        }
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&path, f.body)?;
        wrote += 1;
    }
    Ok((wrote, skipped))
}

/// The language, and that language's choices: each question can be answered by a flag.
fn ask(c: &Choices) -> Result<Stack> {
    let lang = ui::select(
        "Which language do you want to use?",
        &[
            choice(Lang::Rust, "Rust".into(), "Builders like GPUI, or HTML and Tailwind"),
            choice(Lang::Js, "JavaScript or TypeScript".into(), "React, Svelte or plain DOM"),
            choice(Lang::Python, "Python".into(), "Blender's components, or Qt or tkinter style"),
            choice(Lang::Lua, "Lua".into(), "Small, quick to start"),
            choice(Lang::C, "C".into(), "BUI's own API, built with CMake"),
        ],
        c.language.as_ref(),
    )?;
    Ok(match lang {
        Lang::Rust => Stack::Rust(RustApp {}),
        Lang::Lua => Stack::Lua(LuaApp {}),
        Lang::C => Stack::C(CApp {}),
        Lang::Python => Stack::Python(PythonApp {
            api: ui::select(
                "How do you want to write the UI?",
                &[
                    choice(PyApi::Components, "Blender's components".into(), "Panels, sliders, color swatches: Blender's own look"),
                    choice(PyApi::Qt, "Qt style".into(), "Qt code where only the import changes (no PySide needed)"),
                    choice(PyApi::Tk, "tkinter style".into(), "tkinter code where only the import changes"),
                ],
                c.api.as_ref(),
            )?,
        }),
        Lang::Js => ask_js(c)?,
    })
}

fn ask_js(c: &Choices) -> Result<Stack> {
    let framework = ui::select(
        "Which framework?",
        &[
            choice(Framework::React, "React".into(), "React 19: components and hooks"),
            choice(Framework::Svelte, "Svelte".into(), "Svelte 5: runes (runs on Node.js)"),
            choice(Framework::Vanilla, "None".into(), "BUI's own DOM-like API"),
        ],
        c.framework.as_ref(),
    )?;
    // Svelte files are compiled by BUI's loader and written in plain JavaScript
    let language = if framework == Framework::Svelte {
        Language::JavaScript
    } else {
        ui::select(
            "Do you want TypeScript?",
            &[
                choice(Language::TypeScript, "Yes, TypeScript".into(), "recommended: catches mistakes while you type"),
                choice(Language::JavaScript, "No, plain JavaScript".into(), ""),
            ],
            c.language_choice().as_ref(),
        )?
    };
    if framework == Framework::Svelte && c.pm == Some(PackageManager::Bun) {
        bail!("Svelte runs on Node.js (its loader does not work under Bun yet): use --pm npm or --pm pnpm");
    }
    let have = |bin: &str| if which(bin).is_some() { "installed" } else { "not installed yet: I can install it" };
    let mut runtimes = vec![
        choice(PackageManager::Npm, "Node.js with npm".into(), have("node")),
        choice(PackageManager::Pnpm, "Node.js with pnpm".into(), have("pnpm")),
    ];
    // Svelte's loader runs on Node only (it fails under Bun)
    if framework != Framework::Svelte {
        runtimes.insert(0, choice(PackageManager::Bun, "Bun".into(), format!("fastest, runs TypeScript directly; {}", have("bun"))));
    }
    let pm = ui::select("Which runtime and package manager?", &runtimes, c.pm.as_ref())?;
    Ok(Stack::Js(JsApp { framework, language, pm }))
}

fn git_init(dir: &Path, skip: bool) -> Result<()> {
    if skip || which("git").is_none() || Cmd::new("git", ["rev-parse", "--git-dir"]).in_dir(dir).capture().is_some() {
        return Ok(());
    }
    if ui::confirm("Start a git repository?", true)? {
        ui::run("Starting a git repository", &Cmd::new("git", ["init", "--quiet"]).in_dir(dir))?;
    }
    Ok(())
}

/// Runs the install steps; stops at the first that fails and says how to finish by hand.
fn install(steps: &[Step]) -> Result<bool> {
    for (i, s) in steps.iter().enumerate() {
        if !s.work.perform(&s.title)? {
            if s.optional {
                ui::info("That one is optional: your app works without it.");
                continue;
            }
            let rest: Vec<String> = steps[i..].iter().map(|s| ui::code(&s.work.display())).collect();
            ui::warn(format!("Finish the install later with:\n  {}", rest.join("\n  ")));
            return Ok(false);
        }
    }
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_are_checked() {
        assert!(bad_name("my-app").is_none());
        assert!(bad_name("app_2").is_none());
        for bad in ["", "My-App", "2fast", "-x", "a b", "a/b"] {
            assert!(bad_name(bad).is_some(), "{bad}");
        }
    }

    #[test]
    fn a_folder_name_becomes_an_app_name() {
        assert_eq!(sanitize("My App"), "my-app");
        assert_eq!(sanitize("2048"), "app-2048");
        assert_eq!(sanitize("__x__"), "__x__");
    }

    #[test]
    fn the_next_steps_name_every_command() {
        let text = console::strip_ansi_codes(&next_steps("demo", true)).into_owned();
        for cmd in ["cd demo", "bui dev", "bui build", "bui doctor", "bui update", "bui docs"] {
            assert!(text.contains(cmd), "{cmd} missing from {text}");
        }
    }
}
