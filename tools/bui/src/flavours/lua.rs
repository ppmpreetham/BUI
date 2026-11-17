//! Lua: LuaRocks into a `lua_modules` folder of the project (nothing global), Luacheck for lint.

use std::path::Path;

use anyhow::{Result, bail};
use serde::{Deserialize, Serialize};

use super::{File, Flavour, Step, Vars, optional, render, step};
use crate::check::{Check, Fix};
use crate::registry;
use crate::sys::{Cmd, semver};
use crate::tools::Tool;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct LuaApp {}

const TREE: &str = "lua_modules";

fn rocks(dir: &Path, args: &[&str]) -> Cmd {
    let mut all = vec!["--tree", TREE];
    all.extend_from_slice(args);
    Cmd::new("luarocks", all).in_dir(dir)
}

/// `LUA_PATH` and `LUA_CPATH` for the project's rocks, as `luarocks path` prints them.
fn rock_env(dir: &Path) -> Vec<(String, String)> {
    let Some(out) = rocks(dir, &["path"]).capture() else {
        return Vec::new();
    };
    out.lines()
        .filter_map(|l| {
            let l = l.trim();
            // `export LUA_PATH='...'` (Unix) or `SET "LUA_PATH=..."` (Windows)
            let (k, v) = if let Some(rest) = l.strip_prefix("export ") {
                rest.split_once('=')?
            } else {
                l.strip_prefix("SET \"")?.strip_suffix('"')?.split_once('=')?
            };
            let v = v.trim_matches('\'');
            matches!(k, "LUA_PATH" | "LUA_CPATH" | "PATH").then(|| (k.to_owned(), v.replace("$PATH", &std::env::var("PATH").unwrap_or_default())))
        })
        .collect()
}

/// The version of a rock installed in the project's tree.
fn installed(dir: &Path, rock: &str) -> Option<String> {
    let out = rocks(dir, &["list", "--porcelain", rock]).capture()?;
    out.lines().find_map(|l| {
        let mut f = l.split_whitespace();
        (f.next()? == rock).then(|| f.next().map(|v| v.split('-').next().unwrap_or(v).to_owned()))?
    })
}

fn lua_program() -> &'static str {
    ["lua5.4", "lua54", "lua"].into_iter().find(|p| crate::sys::which(p).is_some()).unwrap_or("lua")
}

impl Flavour for LuaApp {
    fn label(&self) -> String {
        "Lua".into()
    }

    fn tools(&self) -> Vec<Tool> {
        // the rock builds BUI's native module (a C compiler)
        vec![Tool::Lua, Tool::Luarocks, Tool::Cc]
    }

    fn files(&self, vars: &Vars) -> Vec<File> {
        render(&["lua"], vars)
    }

    fn deps(&self, dir: &Path) -> Vec<Step> {
        vec![
            step("Adding blenderui (newest)", rocks(dir, &["install", "blenderui"])),
            optional("Adding Luacheck (newest)", rocks(dir, &["install", "luacheck"])),
        ]
    }

    fn dev(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>> {
        Ok(vec![Cmd::new(lua_program(), ["main.lua"]).in_dir(dir).with_args(args).with_env(rock_env(dir))])
    }

    fn build(&self, _dir: &Path, _args: &[String]) -> Result<Vec<Cmd>> {
        bail!("a Lua app is not built: run it with `bui dev`.")
    }

    fn checks(&self, dir: &Path) -> Vec<Check> {
        let install = Fix::run("installs blenderui into lua_modules", vec![rocks(dir, &["install", "blenderui"])]);
        let Some(have) = installed(dir, "blenderui") else {
            return vec![Check::fail("blenderui rock", "not installed in lua_modules", Some(install))];
        };
        let fix = Fix::run("installs the newest blenderui", vec![rocks(dir, &["install", "blenderui"])]);
        // LuaRocks has no cheap "newest" lookup: every package is released with the same version
        match registry::bui_version() {
            Some(newest) if semver(&newest) > semver(&have) => {
                vec![Check::warn("blenderui rock", format!("{have}, newest is {newest}"), Some(fix))]
            }
            _ => vec![Check::ok("blenderui rock", have)],
        }
    }

    fn update(&self, dir: &Path) -> Vec<Step> {
        vec![step("Updating blenderui", rocks(dir, &["install", "blenderui"]))]
    }

}
