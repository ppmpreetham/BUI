//! `bui dev` and `bui build`: the project's own commands, with the toolchain checked first.

use std::path::Path;
use std::process::ExitCode;

use anyhow::Result;

use crate::flavours::Flavour;
use crate::project::{self, Project};
use crate::sys::{Cmd, OS, Os};
use crate::tools::{self, Tool};
use crate::ui;

pub fn dev(args: &[String]) -> Result<ExitCode> {
    go("Running", |a| a.tools(), |p| p.meta.stack.app().dev(&p.dir, args))
}

pub fn build(args: &[String]) -> Result<ExitCode> {
    go("Building", |a| a.build_tools(), |p| p.meta.stack.app().build(&p.dir, args))
}

const DOCS: &str = "https://bui.preetham.top";

/// `bui docs`: the documentation in the browser (or just its address, where there is no browser to open).
pub fn docs() -> ExitCode {
    ui::info(format!("Documentation: {}", ui::code(DOCS)));
    if ui::interactive() {
        let open = match OS {
            Os::Windows => Cmd::script(&format!("Start-Process '{DOCS}'")),
            Os::Mac => Cmd::new("open", [DOCS]),
            Os::Linux => Cmd::new("xdg-open", [DOCS]),
        };
        if !open.run_inherit().is_ok_and(|s| s.success()) {
            ui::info("Could not open a browser: visit the address above.");
        }
    }
    ExitCode::SUCCESS
}

fn go(verb: &str, needs: impl Fn(&dyn Flavour) -> Vec<Tool>, commands: impl Fn(&Project) -> Result<Vec<Cmd>>) -> Result<ExitCode> {
    let project = project::require(Path::new("."))?;
    let app = project.meta.stack.app();
    let missing = tools::ensure(&needs(app))?;
    if !missing.is_empty() {
        ui::error("Cannot continue without those. `bui doctor` shows what to do.");
        return Ok(ExitCode::FAILURE);
    }
    let cmds = commands(&project)?;
    ui::info(format!("{verb} {} ({})", project.meta.name, app.label()));
    for cmd in &cmds {
        ui::flush();
        let status = cmd.run_inherit()?;
        if !status.success() {
            ui::error(format!("`{}` failed", cmd.display()));
            return Ok(ExitCode::from(u8::try_from(status.code().unwrap_or(1)).unwrap_or(1)));
        }
    }
    Ok(ExitCode::SUCCESS)
}
