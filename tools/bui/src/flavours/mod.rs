//! The languages a BUI project can be in. Each one is a module that says what it needs, what files a new
//! project has, how its dependencies are installed (always the newest), how it runs and builds, and how
//! `bui doctor` checks it. Adding a language is a module here and a variant in `project::Stack`.

pub mod c;
pub mod js;
pub mod lua;
pub mod python;
pub mod rust;

use std::collections::BTreeMap;
use std::path::Path;

use anyhow::Result;
use include_dir::{Dir, include_dir};

use crate::check::{Check, Work};
use crate::sys::Cmd;
use crate::tools::Tool;

static TEMPLATES: Dir<'static> = include_dir!("$CARGO_MANIFEST_DIR/templates");

/// A piece of work with the words to show while it runs.
pub struct Step {
    pub title: String,
    pub work: Work,
    /// A step that may fail without the project being unfinished (a linter that needs a C compiler).
    pub optional: bool,
}

pub fn step(title: &str, cmd: Cmd) -> Step {
    Step { title: title.to_owned(), work: Work::Run(cmd), optional: false }
}

/// A step that is allowed to fail.
pub fn optional(title: &str, cmd: Cmd) -> Step {
    Step { optional: true, ..step(title, cmd) }
}

/// A step that does `work`, such as a change to a file's text.
pub fn work_step(title: &str, work: Work) -> Step {
    Step { title: title.to_owned(), work, optional: false }
}

/// A file the project already had that `adopt` changed: which, and what was done.
pub struct Adopted {
    pub path: &'static str,
    pub note: String,
}

pub struct File {
    pub path: String,
    pub body: String,
}

/// What templates fill in.
pub struct Vars {
    pub name: String,
    /// The name as an identifier (`my_app`).
    pub ident: String,
    /// The name as words (`My App`).
    pub title: String,
    /// The newest BUI release tag (`v0.5.1`), for projects that fetch the sources.
    pub tag: String,
}

impl Vars {
    pub fn new(name: &str, tag: &str) -> Self {
        let ident: String = name.chars().map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '_' }).collect();
        let ident = if ident.starts_with(|c: char| c.is_ascii_digit()) { format!("app_{ident}") } else { ident };
        let title = name
            .split(['-', '_', ' '])
            .filter(|w| !w.is_empty())
            .map(|w| {
                let mut cs = w.chars();
                cs.next().map(|f| f.to_uppercase().chain(cs).collect::<String>()).unwrap_or_default()
            })
            .collect::<Vec<_>>()
            .join(" ");
        Self { name: name.to_owned(), ident, title, tag: tag.to_owned() }
    }

    fn fill(&self, text: &str) -> String {
        text.replace("{{name}}", &self.name)
            .replace("{{ident}}", &self.ident)
            .replace("{{title}}", &self.title)
            .replace("{{tag}}", &self.tag)
    }
}

fn collect(dir: &Dir<'_>, layer: &str, vars: &Vars, out: &mut BTreeMap<String, String>) {
    for file in dir.files() {
        let rel = file.path().strip_prefix(layer).unwrap_or_else(|_| file.path());
        let mut path = rel.to_string_lossy().replace('\\', "/");
        // dotfiles are stored without the dot: a `.gitignore` in the templates would ignore them
        if let Some(rest) = path.strip_prefix("_") {
            path = format!(".{rest}");
        } else if let Some(i) = path.rfind("/_") {
            path = format!("{}/.{}", &path[..i], &path[i + 2..]);
        }
        if let Some(text) = file.contents_utf8() {
            out.insert(path, vars.fill(text));
        }
    }
    for sub in dir.dirs() {
        collect(sub, layer, vars, out);
    }
}

/// The files of template folders laid over each other (a later folder replaces a file of the same name).
pub fn render(layers: &[&str], vars: &Vars) -> Vec<File> {
    let mut files = BTreeMap::new();
    for layer in layers {
        if let Some(dir) = TEMPLATES.get_dir(layer) {
            collect(dir, layer, vars, &mut files);
        }
    }
    files.into_iter().map(|(path, body)| File { path, body }).collect()
}

pub trait Flavour {
    /// What the user chose, in words ("React + TypeScript on Bun").
    fn label(&self) -> String;
    /// The programs needed to make, run and build the project.
    fn tools(&self) -> Vec<Tool>;
    /// The programs `bui build` needs: those of `tools` unless the build needs more (JavaScript bundles with Bun).
    fn build_tools(&self) -> Vec<Tool> {
        self.tools()
    }
    fn files(&self, vars: &Vars) -> Vec<File>;
    /// Makes the project's existing files fit (`bui init` in a folder that has them), before anything is
    /// written: an error stops `init` with nothing changed. Everything else of theirs is left as it is.
    fn adopt(&self, _dir: &Path) -> Result<Vec<Adopted>> {
        Ok(Vec::new())
    }
    /// Installs BUI and the language's tooling at their newest versions, in the new project.
    fn deps(&self, dir: &Path) -> Vec<Step>;
    /// Runs the app (a dev server where the language has one), as a list of commands. `args` are the
    /// user's own, for the last of them.
    fn dev(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>>;
    /// Makes a release build; an error says what to do instead where there is no build step.
    fn build(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>>;
    /// The problems of the project itself (`bui doctor` adds the toolchain's).
    fn checks(&self, dir: &Path) -> Vec<Check>;
    /// Brings BUI and the tooling to their newest versions.
    fn update(&self, dir: &Path) -> Vec<Step>;
}

/// A file's text, if it can be read.
pub fn read(dir: &Path, file: &str) -> Option<String> {
    std::fs::read_to_string(dir.join(file)).ok()
}
