//! What `bui doctor` is made of: a check says how something is and, when it is not right, the work
//! that makes it right.

use std::fs;
use std::path::PathBuf;

use anyhow::{Context, Result};

use crate::sys::{Cmd, semver};
use crate::tools::Tool;
use crate::ui;

/// One piece of work: a command, or a change to a file that no command can make portably.
pub enum Work {
    Run(Cmd),
    Rewrite { path: PathBuf, from: String, to: String },
}

impl Work {
    /// What the user would type, or what changes.
    pub fn display(&self) -> String {
        match self {
            Self::Run(c) => c.display(),
            Self::Rewrite { path, from, to } => format!("edit {}: {from} -> {to}", path.display()),
        }
    }

    /// Does it under a spinner titled `title`; false when it failed (the reason was shown).
    pub fn perform(&self, title: &str) -> Result<bool> {
        match self {
            Self::Run(cmd) => ui::run(title, cmd),
            Self::Rewrite { path, from, to } => {
                let text = fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
                fs::write(path, text.replacen(from.as_str(), to, 1)).with_context(|| format!("writing {}", path.display()))?;
                ui::success(title);
                Ok(true)
            }
        }
    }
}

/// Work that fixes a problem, and why it does.
pub struct Fix {
    pub why: String,
    pub work: Vec<Work>,
    /// An installer: it gets the terminal (it may ask for a password) instead of a spinner.
    pub interactive: bool,
}

impl Fix {
    pub fn run(why: &str, cmds: Vec<Cmd>) -> Self {
        Self { why: why.to_owned(), work: cmds.into_iter().map(Work::Run).collect(), interactive: false }
    }

    pub fn install(why: &str, cmds: Vec<Cmd>) -> Self {
        Self { interactive: true, ..Self::run(why, cmds) }
    }

    pub const fn edit(why: String, work: Vec<Work>) -> Self {
        Self { why, work, interactive: false }
    }
}

pub enum Health {
    Ok(String),
    /// Works, but should be better (an old version).
    Warn(String, Option<Fix>),
    /// Does not work.
    Fail(String, Option<Fix>),
}

pub struct Check {
    pub name: String,
    pub health: Health,
}

impl Check {
    pub fn ok(name: &str, detail: impl Into<String>) -> Self {
        Self { name: name.to_owned(), health: Health::Ok(detail.into()) }
    }

    pub fn warn(name: &str, detail: impl Into<String>, fix: Option<Fix>) -> Self {
        Self { name: name.to_owned(), health: Health::Warn(detail.into(), fix) }
    }

    pub fn fail(name: &str, detail: impl Into<String>, fix: Option<Fix>) -> Self {
        Self { name: name.to_owned(), health: Health::Fail(detail.into(), fix) }
    }

    pub const fn is_ok(&self) -> bool {
        matches!(self.health, Health::Ok(_))
    }

    pub const fn fix(&self) -> Option<&Fix> {
        match &self.health {
            Health::Warn(_, f) | Health::Fail(_, f) => f.as_ref(),
            Health::Ok(_) => None,
        }
    }
}

/// A tool is installed, or here is how to install the newest.
pub fn tool(t: Tool) -> Check {
    match t.probe() {
        Some(v) => Check::ok(t.label(), v),
        None => Check::fail(
            t.label(),
            "not installed",
            Some(Fix::install(&format!("installs the newest {}", t.label()), t.install().cmds)),
        ),
    }
}

/// `have` is the newest version, or here is how to get it. A registry that could not be reached is not
/// a problem.
pub fn newest(name: &str, have: &str, latest: Option<&str>, fix: Fix) -> Check {
    match latest {
        Some(l) if semver(l) > semver(have) => Check::warn(name, format!("{have}, newest is {l}"), Some(fix)),
        Some(_) => Check::ok(name, format!("{have} (newest)")),
        None => Check::ok(name, format!("{have} (could not look up the newest)")),
    }
}
