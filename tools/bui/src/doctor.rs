//! `bui doctor`: look at what the current project needs (and only that, unless asked for all), say what
//! is wrong in a line each, and offer to run the fixes.

use std::path::Path;
use std::process::ExitCode;

use anyhow::Result;
use serde_json::json;

use crate::check::{self, Check, Fix, Health, Work};
use crate::cli::DoctorArgs;
use crate::project;
use crate::registry;
use crate::sys::{Cmd, OS, Os, semver};
use crate::tools::Tool;
use crate::ui;

/// Every toolchain bui knows, for `--all` and for a folder that is not a project.
const ALL: [Tool; 9] =
    [Tool::Rust, Tool::Cc, Tool::Cmake, Tool::Bun, Tool::Node, Tool::Uv, Tool::Lua, Tool::Luarocks, Tool::Git];

struct Section {
    title: String,
    checks: Vec<Check>,
}

pub fn run(args: &DoctorArgs) -> Result<ExitCode> {
    if !args.json {
        ui::banner(false);
        ui::intro("bui doctor");
    }
    let project = project::find(Path::new("."));
    let mut sections = gather(project.as_ref(), args.all);
    if args.json {
        print_json(&sections);
        return Ok(exit_code(&sections));
    }
    show(&sections);
    let fixable: Vec<&Fix> = sections.iter().flat_map(|s| &s.checks).filter_map(Check::fix).collect();
    if fixable.is_empty() {
        return Ok(finish(&sections));
    }
    let list: Vec<String> = fixable
        .iter()
        .enumerate()
        .map(|(i, f)| {
            let cmds: Vec<String> = f.work.iter().map(|w| format!("      $ {}", ui::code(&w.display()))).collect();
            format!("{}. {}\n{}", i + 1, f.why, cmds.join("\n"))
        })
        .collect();
    ui::note("What would fix it", &list.join("\n"));
    if args.fix || (ui::asking() && ui::confirm(&format!("Run {} fix{} now?", fixable.len(), if fixable.len() == 1 { "" } else { "es" }), true)?) {
        for fix in &fixable {
            for w in &fix.work {
                if !perform(fix, w)? {
                    break;
                }
            }
        }
        // look again: a fix may have fixed more than it said, or not worked
        ui::info("Checking again");
        sections = gather(project.as_ref(), args.all);
        show(&sections);
    }
    Ok(finish(&sections))
}

fn perform(fix: &Fix, work: &Work) -> Result<bool> {
    match work {
        // an installer may ask for a password or show its own progress: give it the terminal
        Work::Run(cmd) if fix.interactive => {
            ui::flush();
            let ok = cmd.run_inherit()?.success();
            if !ok {
                ui::error(format!("`{}` failed", cmd.display()));
            }
            Ok(ok)
        }
        w => w.perform(&fix.why),
    }
}

fn gather(project: Option<&project::Project>, all: bool) -> Vec<Section> {
    let task = ui::task("Looking around");
    let mut sections = vec![Section { title: "bui".into(), checks: bui_checks() }];
    if let Some(p) = project {
        let app = p.meta.stack.app();
        task.set("Checking the toolchain");
        sections.push(Section { title: format!("{} ({})", p.meta.name, app.label()), checks: Vec::new() });
        let toolchain: Vec<Check> = app.build_tools().into_iter().map(check::tool).collect();
        let toolchain_ok = toolchain.iter().all(Check::is_ok);
        let mut checks = toolchain;
        // a project's own checks need its tools (they run them)
        if toolchain_ok {
            task.set("Checking the project");
            checks.extend(app.checks(&p.dir));
        }
        if let Some(s) = sections.last_mut() {
            s.checks = checks;
        }
        if !p.declared {
            ui::flush();
        }
    }
    if all || project.is_none() {
        task.set("Checking every toolchain");
        sections.push(Section { title: "Toolchains".into(), checks: ALL.into_iter().map(check::tool).collect() });
    }
    if OS == Os::Linux && std::env::var_os("DISPLAY").is_none() && std::env::var_os("WAYLAND_DISPLAY").is_none() {
        sections.push(Section {
            title: "Display".into(),
            checks: vec![Check::warn("window system", "no DISPLAY or WAYLAND_DISPLAY: windows cannot open here (SSH? WSL without WSLg?). Headless rendering still works.", None)],
        });
    }
    let bad = sections.iter().flat_map(|s| &s.checks).filter(|c| !c.is_ok()).count();
    task.done(&if bad == 0 { "Checked".to_owned() } else { format!("Checked: {bad} to look at") });
    sections
}

/// This `bui` against the newest release.
fn bui_checks() -> Vec<Check> {
    let have = env!("CARGO_PKG_VERSION");
    let exe = std::env::current_exe().map_or_else(|_| "bui".into(), |p| p.to_string_lossy().into_owned());
    let fix = Fix::run("updates bui", vec![Cmd::new(&exe, ["update", "--yes", "--cli-only"])]);
    match registry::release_tag() {
        Some(tag) if semver(&tag) > semver(have) => vec![Check::warn("bui", format!("{have}, newest is {}", tag.trim_start_matches('v')), Some(fix))],
        Some(_) => vec![Check::ok("bui", format!("{have} (newest)"))],
        None => vec![Check::ok("bui", format!("{have} (no newer release found, or offline)"))],
    }
}

/// One check as a line.
pub fn line(c: &Check) -> String {
    match &c.health {
        Health::Ok(d) => format!("{} {}  {}", console::style("✔").green(), c.name, ui::dim(d)),
        Health::Warn(d, _) => format!("{} {}  {}", console::style("▲").yellow(), c.name, d),
        Health::Fail(d, _) => format!("{} {}  {}", console::style("✘").red(), c.name, d),
    }
}

fn show(sections: &[Section]) {
    for s in sections {
        let lines: Vec<String> = s.checks.iter().map(line).collect();
        ui::note(&s.title, &lines.join("\n"));
    }
}

fn finish(sections: &[Section]) -> ExitCode {
    let bad = sections.iter().flat_map(|s| &s.checks).filter(|c| matches!(c.health, Health::Fail(..))).count();
    let warn = sections.iter().flat_map(|s| &s.checks).filter(|c| matches!(c.health, Health::Warn(..))).count();
    match (bad, warn) {
        (0, 0) => ui::outro(&format!("{} All good", ui::accent("✦"))),
        (0, w) => ui::outro(&format!("Works, with {w} thing{} to look at", if w == 1 { "" } else { "s" })),
        (b, _) => ui::outro(&format!("{b} problem{} left", if b == 1 { "" } else { "s" })),
    }
    exit_code(sections)
}

fn exit_code(sections: &[Section]) -> ExitCode {
    if sections.iter().flat_map(|s| &s.checks).any(|c| matches!(c.health, Health::Fail(..))) { ExitCode::FAILURE } else { ExitCode::SUCCESS }
}

fn print_json(sections: &[Section]) {
    let out: Vec<_> = sections
        .iter()
        .map(|s| {
            let checks: Vec<_> = s
                .checks
                .iter()
                .map(|c| {
                    let (status, detail) = match &c.health {
                        Health::Ok(d) => ("ok", d),
                        Health::Warn(d, _) => ("warn", d),
                        Health::Fail(d, _) => ("fail", d),
                    };
                    let fix: Vec<String> = c.fix().map(|f| f.work.iter().map(Work::display).collect()).unwrap_or_default();
                    json!({ "name": c.name, "status": status, "detail": detail, "fix": fix })
                })
                .collect();
            json!({ "section": s.title, "checks": checks })
        })
        .collect();
    println!("{}", serde_json::to_string_pretty(&out).unwrap_or_default());
}
