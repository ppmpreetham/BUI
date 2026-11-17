//! A BUI project: what `bui.toml` says about it (which language and options it was made with), found
//! from any folder inside it, or guessed from its files when it has none.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use clap::ValueEnum;
use serde::{Deserialize, Serialize};

use crate::flavours::{Flavour, c::CApp, js::JsApp, lua::LuaApp, python::PythonApp, rust::RustApp};

pub const FILE: &str = "bui.toml";

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize, ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum Framework {
    React,
    Svelte,
    /// BUI's DOM-like API with no framework.
    Vanilla,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize, ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum Language {
    #[value(name = "ts")]
    TypeScript,
    #[value(name = "js")]
    JavaScript,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize, ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum PackageManager {
    Bun,
    Npm,
    Pnpm,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize, ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum PyApi {
    /// Blender's own components.
    Components,
    Qt,
    Tk,
}

/// The language a project is in, with that language's options.
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "flavour", rename_all = "lowercase")]
pub enum Stack {
    Rust(RustApp),
    Js(JsApp),
    Python(PythonApp),
    Lua(LuaApp),
    C(CApp),
}

impl Stack {
    pub fn app(&self) -> &dyn Flavour {
        match self {
            Self::Rust(a) => a,
            Self::Js(a) => a,
            Self::Python(a) => a,
            Self::Lua(a) => a,
            Self::C(a) => a,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Meta {
    pub name: String,
    #[serde(flatten)]
    pub stack: Stack,
}

#[derive(Serialize, Deserialize)]
struct Doc {
    project: Meta,
}

pub struct Project {
    pub dir: PathBuf,
    pub meta: Meta,
    /// Read from `bui.toml` (not guessed).
    pub declared: bool,
}

impl Meta {
    pub fn to_toml(&self) -> Result<String> {
        let body = toml::to_string_pretty(&Doc { project: self.clone() })?;
        Ok(format!("# Made by `bui new`: bui reads this to know how to run, build and check the project.\n{body}"))
    }
}

/// The project that contains `from`: the nearest folder up with a `bui.toml`, else the folder itself
/// when its files say what it is.
pub fn find(from: &Path) -> Option<Project> {
    let start = from.canonicalize().unwrap_or_else(|_| from.to_owned());
    // Windows' canonical paths start with `\\?\`, which many tools cannot read
    let start = start.to_string_lossy().strip_prefix(r"\\?\").map_or_else(|| start.clone(), PathBuf::from);
    for dir in start.ancestors() {
        if let Some(doc) = fs::read_to_string(dir.join(FILE)).ok().and_then(|t| toml::from_str::<Doc>(&t).ok()) {
            return Some(Project { dir: dir.to_owned(), meta: doc.project, declared: true });
        }
    }
    guess(&start).map(|meta| Project { dir: start, meta, declared: false })
}

fn read(dir: &Path, file: &str) -> String {
    fs::read_to_string(dir.join(file)).unwrap_or_default()
}

fn guess(dir: &Path) -> Option<Meta> {
    let name = dir.file_name()?.to_string_lossy().into_owned();
    let stack = if read(dir, "Cargo.toml").contains("blenderui") {
        Stack::Rust(RustApp {})
    } else if read(dir, "package.json").contains("@blender-ui/") {
        let pkg = read(dir, "package.json");
        let framework = if pkg.contains("@blender-ui/react") {
            Framework::React
        } else if pkg.contains("@blender-ui/svelte") {
            Framework::Svelte
        } else {
            Framework::Vanilla
        };
        let pm = if dir.join("bun.lock").exists() || dir.join("bun.lockb").exists() {
            PackageManager::Bun
        } else if dir.join("pnpm-lock.yaml").exists() {
            PackageManager::Pnpm
        } else {
            PackageManager::Npm
        };
        let language = if dir.join("tsconfig.json").exists() { Language::TypeScript } else { Language::JavaScript };
        Stack::Js(JsApp { framework, language, pm })
    } else if depends_on_blenderui(&read(dir, "pyproject.toml")) {
        Stack::Python(PythonApp { api: PyApi::Components })
    } else if read(dir, "main.lua").contains("require(\"bui\")") || read(dir, "main.lua").contains("require('bui')") {
        Stack::Lua(LuaApp {})
    } else if read(dir, "CMakeLists.txt").contains("BUI") && dir.join("src").is_dir() {
        Stack::C(CApp {})
    } else {
        return None;
    };
    Some(Meta { name, stack })
}

/// A `pyproject.toml` that uses blenderui (not the one of blenderui itself, which is named that).
fn depends_on_blenderui(pyproject: &str) -> bool {
    pyproject.contains("blenderui") && !pyproject.contains("name = \"blenderui\"")
}

pub fn require(from: &Path) -> Result<Project> {
    find(from).with_context(|| "this folder is not a BUI project (no bui.toml). Make one with `bui new` or `bui init`.")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::flavours::{Vars, c::CApp};

    pub fn every_stack() -> Vec<Stack> {
        let mut stacks = vec![Stack::Rust(RustApp {}), Stack::Lua(LuaApp {}), Stack::C(CApp {})];
        for api in [PyApi::Components, PyApi::Qt, PyApi::Tk] {
            stacks.push(Stack::Python(PythonApp { api }));
        }
        for pm in [PackageManager::Bun, PackageManager::Npm, PackageManager::Pnpm] {
            for (framework, language) in [
                (Framework::React, Language::TypeScript),
                (Framework::React, Language::JavaScript),
                (Framework::Svelte, Language::JavaScript),
                (Framework::Vanilla, Language::TypeScript),
                (Framework::Vanilla, Language::JavaScript),
            ] {
                stacks.push(Stack::Js(JsApp { framework, language, pm }));
            }
        }
        stacks
    }

    #[test]
    fn bui_toml_round_trips_for_every_stack() {
        for stack in every_stack() {
            let meta = Meta { name: "demo".into(), stack };
            let text = meta.to_toml().unwrap();
            let back: Doc = toml::from_str(&text).unwrap();
            assert_eq!(back.project.stack.app().label(), meta.stack.app().label(), "{text}");
        }
    }

    #[test]
    fn every_stack_renders_files_with_no_placeholder_left() {
        let vars = Vars::new("my-app", "v1.2.3");
        for stack in every_stack() {
            let files = stack.app().files(&vars);
            assert!(!files.is_empty(), "{}", stack.app().label());
            assert!(files.iter().any(|f| f.path == ".gitignore"), "{} has no .gitignore", stack.app().label());
            for f in &files {
                assert!(!f.body.contains("{{"), "{}: {} has a placeholder left", stack.app().label(), f.path);
                assert!(!f.path.contains("/_") && !f.path.starts_with('_'), "{}: {} keeps its underscore", stack.app().label(), f.path);
            }
        }
    }

    #[test]
    fn blenderuis_own_pyproject_is_not_an_app() {
        assert!(!depends_on_blenderui("[project]
name = \"blenderui\"
"));
        assert!(depends_on_blenderui("[project]
name = \"x\"
dependencies = [\"blenderui>=0.5\"]
"));
    }

    #[test]
    fn a_project_is_found_from_a_folder_inside_it() {
        let dir = std::env::temp_dir().join(format!("bui-test-{}", std::process::id()));
        let inner = dir.join("src/deep");
        fs::create_dir_all(&inner).unwrap();
        let meta = Meta { name: "demo".into(), stack: Stack::Lua(LuaApp {}) };
        fs::write(dir.join(FILE), meta.to_toml().unwrap()).unwrap();
        let found = find(&inner).expect("found");
        assert!(found.declared);
        assert_eq!(found.meta.name, "demo");
        fs::remove_dir_all(&dir).unwrap();
    }
}
