//! The machine: the operating system, finding programs and running commands.

use std::collections::VecDeque;
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus, Stdio};
use std::sync::mpsc;
use std::thread;

use anyhow::{Context, Result};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Os {
    Windows,
    Mac,
    Linux,
}

pub const OS: Os = if cfg!(windows) {
    Os::Windows
} else if cfg!(target_os = "macos") {
    Os::Mac
} else {
    Os::Linux
};

pub fn home() -> Option<PathBuf> {
    std::env::home_dir()
}

/// Where tools put themselves that a terminal started before the install does not search yet.
fn extra_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if let Some(h) = home() {
        for d in [".cargo/bin", ".bun/bin", ".local/bin", ".bui/bin", "scoop/shims", ".deno/bin", ".local/share/fnm/aliases/default/bin"] {
            dirs.push(h.join(d));
        }
    }
    if OS == Os::Windows {
        for var in ["ProgramFiles", "ProgramFiles(x86)"] {
            if let Some(p) = std::env::var_os(var).map(PathBuf::from) {
                for d in ["nodejs", "Git/cmd", "CMake/bin", "LuaRocks"] {
                    dirs.push(p.join(d));
                }
            }
        }
    }
    if OS == Os::Mac {
        dirs.push(PathBuf::from("/opt/homebrew/bin"));
        dirs.push(PathBuf::from("/usr/local/bin"));
    }
    dirs
}

fn candidates(dir: &Path, name: &str) -> Vec<PathBuf> {
    if OS == Os::Windows {
        ["exe", "cmd", "bat", "com"].iter().map(|e| dir.join(format!("{name}.{e}"))).collect()
    } else {
        vec![dir.join(name)]
    }
}

/// The program `name` on `PATH` (or where installers put it), as the shell would find it.
pub fn which(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os("PATH").unwrap_or_default();
    std::env::split_paths(&path)
        .chain(extra_dirs())
        .flat_map(|d| candidates(&d, name))
        .find(|p| p.is_file())
}

/// A command to run: a program and its arguments, or a line for the shell (installers pipe).
#[derive(Clone, Debug)]
pub struct Cmd {
    program: String,
    args: Vec<String>,
    script: bool,
    pub cwd: Option<PathBuf>,
    pub env: Vec<(String, String)>,
}

impl Cmd {
    pub fn new<I, S>(program: &str, args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        Self {
            program: program.to_owned(),
            args: args.into_iter().map(Into::into).collect(),
            script: false,
            cwd: None,
            env: Vec::new(),
        }
    }

    /// A line for `sh -c` (Windows: PowerShell).
    pub fn script(line: &str) -> Self {
        Self { program: line.to_owned(), args: Vec::new(), script: true, cwd: None, env: Vec::new() }
    }

    #[must_use]
    pub fn in_dir(mut self, dir: &Path) -> Self {
        self.cwd = Some(dir.to_owned());
        self
    }

    /// More arguments after the ones it has.
    #[must_use]
    pub fn with_args(mut self, more: &[String]) -> Self {
        self.args.extend(more.iter().cloned());
        self
    }

    #[must_use]
    pub fn with_env(mut self, vars: Vec<(String, String)>) -> Self {
        self.env = vars;
        self
    }

    /// What the user would type.
    pub fn display(&self) -> String {
        if self.script {
            return self.program.clone();
        }
        std::iter::once(self.program.as_str())
            .chain(self.args.iter().map(String::as_str))
            .map(|a| if a.contains(' ') { format!("\"{a}\"") } else { a.to_owned() })
            .collect::<Vec<_>>()
            .join(" ")
    }

    fn command(&self) -> Result<Command> {
        let mut c = if self.script {
            if OS == Os::Windows {
                let mut c = Command::new("powershell");
                c.args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", &self.program]);
                c
            } else {
                let mut c = Command::new("sh");
                c.args(["-c", &self.program]);
                c
            }
        } else {
            let given = Path::new(&self.program);
            let exe = if given.is_absolute() { given.to_owned() } else { which(&self.program).with_context(|| format!("`{}` is not installed", self.program))? };
            let mut c = Command::new(exe);
            c.args(&self.args);
            c
        };
        if let Some(d) = &self.cwd {
            c.current_dir(d);
        }
        c.envs(self.env.iter().map(|(k, v)| (k, v)));
        Ok(c)
    }

    /// Runs with the terminal's own input and output (a dev server, an installer asking for sudo).
    pub fn run_inherit(&self) -> Result<ExitStatus> {
        self.command()?.status().with_context(|| format!("running {}", self.display()))
    }

    /// Runs quietly and returns the output when it succeeds.
    pub fn capture(&self) -> Option<String> {
        let out = self.command().ok()?.stdin(Stdio::null()).output().ok()?;
        out.status.success().then(|| String::from_utf8_lossy(&out.stdout).into_owned())
    }

    /// Runs with each line it prints handed to `on_line`, and the last lines kept for an error.
    pub fn run_live(&self, mut on_line: impl FnMut(&str)) -> Result<Outcome> {
        let mut child = self
            .command()?
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .with_context(|| format!("starting {}", self.display()))?;
        let (tx, rx) = mpsc::channel::<String>();
        let streams: [Option<Box<dyn Read + Send>>; 2] = [
            child.stdout.take().map(|s| Box::new(s) as Box<dyn Read + Send>),
            child.stderr.take().map(|s| Box::new(s) as Box<dyn Read + Send>),
        ];
        let readers: Vec<_> = streams
            .into_iter()
            .flatten()
            .map(|stream| {
                let tx = tx.clone();
                thread::spawn(move || {
                    for line in BufReader::new(stream).split(b'\n').map_while(Result::ok) {
                        let text = String::from_utf8_lossy(&line).into_owned();
                        // progress bars redraw with \r: the last redraw is the line
                        let last = text.rsplit('\r').find(|s| !s.trim().is_empty()).unwrap_or("");
                        let _ = tx.send(console::strip_ansi_codes(last).trim_end().to_owned());
                    }
                })
            })
            .collect();
        drop(tx);
        let mut tail = VecDeque::with_capacity(TAIL);
        for line in rx {
            if line.is_empty() {
                continue;
            }
            on_line(&line);
            if tail.len() == TAIL {
                tail.pop_front();
            }
            tail.push_back(line);
        }
        for r in readers {
            let _ = r.join();
        }
        let status = child.wait()?;
        Ok(Outcome { ok: status.success(), tail: tail.into() })
    }
}

const TAIL: usize = 24;

pub struct Outcome {
    pub ok: bool,
    /// The last lines it printed.
    pub tail: Vec<String>,
}

/// The first `1.2.3`-looking token of a program's `--version`.
pub fn version_of(program: &str, args: &[&str]) -> Option<String> {
    let out = Cmd::new(program, args.iter().copied()).capture()?;
    out.split(|c: char| !(c.is_ascii_digit() || c == '.'))
        .find(|t| t.contains('.') && t.chars().next().is_some_and(|c| c.is_ascii_digit()))
        .map(|t| t.trim_end_matches('.').to_owned())
}

/// `a.b.c` as numbers, to compare versions.
pub fn semver(v: &str) -> [u64; 3] {
    let mut parts = v.trim_start_matches('v').split(['.', '-', '+']).map(|p| p.parse().unwrap_or(0));
    [parts.next().unwrap_or(0), parts.next().unwrap_or(0), parts.next().unwrap_or(0)]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_compare_as_numbers() {
        assert!(semver("0.10.0") > semver("0.9.9"));
        assert!(semver("v1.0.0") > semver("0.99.99"));
        assert_eq!(semver("1.2"), [1, 2, 0]);
        assert_eq!(semver("0.5.1-beta"), [0, 5, 1]);
    }

    #[test]
    fn a_command_shows_as_typed() {
        assert_eq!(Cmd::new("cargo", ["add", "blenderui"]).display(), "cargo add blenderui");
        assert_eq!(Cmd::new("x", ["a b"]).display(), "x \"a b\"");
    }
}
