//! The programs a language needs (Rust's cargo, Bun, uv, ...): whether they are there, the commands
//! that install the newest one on this machine, and the question that offers to run them.

use std::sync::atomic::{AtomicBool, Ordering};

use anyhow::Result;

use crate::sys::{Cmd, OS, Os, version_of, which};
use crate::ui;

static INSTALL_WITHOUT_ASKING: AtomicBool = AtomicBool::new(false);

/// `--install-tools`: install what is missing without the question (scripts, CI).
pub fn install_without_asking() {
    INSTALL_WITHOUT_ASKING.store(true, Ordering::Relaxed);
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Tool {
    Rust,
    Bun,
    Node,
    Pnpm,
    Uv,
    Lua,
    Luarocks,
    Cmake,
    /// A C compiler (on Windows, Visual Studio's C++ tools).
    Cc,
    Git,
}

/// How to install a tool: commands, in order.
pub struct Install {
    pub cmds: Vec<Cmd>,
    pub note: Option<&'static str>,
}

fn pkg(apt: &str, dnf: &str, pacman: &str) -> Cmd {
    let line = if which("apt-get").is_some() {
        format!("sudo apt-get install -y {apt}")
    } else if which("dnf").is_some() {
        format!("sudo dnf install -y {dnf}")
    } else if which("pacman").is_some() {
        format!("sudo pacman -S --needed --noconfirm {pacman}")
    } else {
        format!("sudo apt-get install -y {apt}")
    };
    Cmd::script(&line)
}

fn winget(id: &str) -> Cmd {
    Cmd::new("winget", ["install", "-e", "--id", id, "--accept-source-agreements", "--accept-package-agreements"])
}

fn brew(formulae: &str) -> Cmd {
    Cmd::script(&format!("brew install {formulae}"))
}

impl Tool {
    pub const fn label(self) -> &'static str {
        match self {
            Self::Rust => "Rust (cargo)",
            Self::Bun => "Bun",
            Self::Node => "Node.js",
            Self::Pnpm => "pnpm",
            Self::Uv => "uv",
            Self::Lua => "Lua 5.4",
            Self::Luarocks => "LuaRocks",
            Self::Cmake => "CMake",
            Self::Cc => "a C compiler",
            Self::Git => "Git",
        }
    }

    /// The version when it is installed (`"found"` when it has none to print).
    pub fn probe(self) -> Option<String> {
        let v = |program: &str, arg: &str| which(program).map(|_| version_of(program, &[arg]).unwrap_or_else(|| "found".into()));
        match self {
            Self::Rust => v("cargo", "--version"),
            Self::Bun => v("bun", "--version"),
            Self::Node => v("node", "--version"),
            Self::Pnpm => v("pnpm", "--version"),
            Self::Uv => v("uv", "--version"),
            Self::Lua => ["lua5.4", "lua54", "lua"]
                .into_iter()
                .find_map(|p| which(p).and_then(|_| version_of(p, &["-v"]).filter(|ver| ver.starts_with("5.4")))),
            Self::Luarocks => v("luarocks", "--version"),
            Self::Cmake => v("cmake", "--version"),
            Self::Git => v("git", "--version"),
            Self::Cc => cc_probe(),
        }
    }

    pub fn install(self) -> Install {
        let none = |cmds: Vec<Cmd>| Install { cmds, note: None };
        match (self, OS) {
            (Self::Rust, Os::Windows) => Install {
                cmds: vec![winget("Rustlang.Rustup")],
                note: Some("Rust on Windows also needs Visual Studio's C++ build tools (bui offers them next)."),
            },
            (Self::Rust, _) => none(vec![Cmd::script("curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y")]),
            (Self::Bun, Os::Windows) => none(vec![Cmd::script("irm bun.sh/install.ps1 | iex")]),
            (Self::Bun, _) => none(vec![Cmd::script("curl -fsSL https://bun.sh/install | bash")]),
            (Self::Node, Os::Windows) => none(vec![winget("OpenJS.NodeJS.LTS")]),
            (Self::Node, Os::Mac) if which("brew").is_some() => none(vec![brew("node")]),
            (Self::Node, _) => none(vec![Cmd::script(
                "curl -fsSL https://fnm.vercel.app/install | bash && export PATH=\"$HOME/.local/share/fnm:$PATH\" && eval \"$(fnm env)\" && fnm install --lts && fnm default lts-latest",
            )]),
            (Self::Pnpm, _) => none(vec![Cmd::new("npm", ["install", "--global", "pnpm"])]),
            (Self::Uv, Os::Windows) => none(vec![Cmd::script("irm https://astral.sh/uv/install.ps1 | iex")]),
            (Self::Uv, _) => none(vec![Cmd::script("curl -LsSf https://astral.sh/uv/install.sh | sh")]),
            (Self::Lua | Self::Luarocks, Os::Windows) => Install {
                cmds: vec![Cmd::script("scoop install lua luarocks")],
                note: Some("Needs Scoop (https://scoop.sh): `irm get.scoop.sh | iex`."),
            },
            (Self::Lua | Self::Luarocks, Os::Mac) => none(vec![brew("lua luarocks")]),
            (Self::Lua | Self::Luarocks, Os::Linux) => none(vec![pkg(
                "lua5.4 liblua5.4-dev luarocks build-essential",
                "lua lua-devel luarocks gcc make",
                "lua luarocks base-devel",
            )]),
            (Self::Cmake, Os::Windows) => none(vec![winget("Kitware.CMake")]),
            (Self::Cmake, Os::Mac) => none(vec![brew("cmake")]),
            (Self::Cmake, Os::Linux) => none(vec![pkg("cmake", "cmake", "cmake")]),
            (Self::Cc, Os::Windows) => Install {
                cmds: vec![Cmd::new(
                    "winget",
                    [
                        "install", "-e", "--id", "Microsoft.VisualStudio.2022.BuildTools", "--override",
                        "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended",
                    ],
                )],
                note: Some("A large download (a few GB); it only has to happen once."),
            },
            (Self::Cc | Self::Git, Os::Mac) => none(vec![Cmd::script("xcode-select --install")]),
            (Self::Cc, Os::Linux) => none(vec![pkg("build-essential", "gcc gcc-c++ make", "base-devel")]),
            (Self::Git, Os::Windows) => none(vec![winget("Git.Git")]),
            (Self::Git, Os::Linux) => none(vec![pkg("git", "git", "git")]),
        }
    }
}

fn cc_probe() -> Option<String> {
    if OS == Os::Windows {
        let root = std::env::var_os("ProgramFiles(x86)")?;
        let vswhere = std::path::Path::new(&root).join("Microsoft Visual Studio/Installer/vswhere.exe");
        let out = Cmd::new(
            vswhere.to_str()?,
            [
                "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath",
            ],
        )
        .capture()?;
        let path = out.trim();
        return (!path.is_empty()).then(|| "Visual Studio C++ tools".to_owned());
    }
    ["cc", "gcc", "clang"].into_iter().find_map(|c| which(c).map(|_| c.to_owned()))
}

/// Looks for each tool; for the missing ones shows how to install the newest, offers to run it and
/// looks again. Returns the tools that are still missing.
pub fn ensure(tools: &[Tool]) -> Result<Vec<Tool>> {
    let mut wanted: Vec<Tool> = Vec::new();
    for t in tools {
        if !wanted.contains(t) {
            wanted.push(*t);
        }
    }
    let missing: Vec<Tool> = wanted.into_iter().filter(|t| t.probe().is_none()).collect();
    if missing.is_empty() {
        return Ok(missing);
    }
    let names: Vec<&str> = missing.iter().map(|t| t.label()).collect();
    ui::warn(format!("Not installed: {}", names.join(", ")));
    let mut done: Vec<String> = Vec::new();
    for tool in &missing {
        let install = tool.install();
        let shown: Vec<String> = install.cmds.iter().map(Cmd::display).collect();
        if shown.iter().all(|s| done.contains(s)) {
            continue; // the same command already installed this one (Lua and LuaRocks)
        }
        let mut body = shown.iter().map(|s| format!("$ {}", ui::code(s))).collect::<Vec<_>>().join("\n");
        if let Some(n) = install.note {
            body = format!("{body}\n{}", ui::dim(n));
        }
        ui::note(&format!("Install the newest {}", tool.label()), &body);
        let go = INSTALL_WITHOUT_ASKING.load(Ordering::Relaxed)
            || (ui::asking() && ui::confirm(&format!("Run it now to install {}?", tool.label()), true)?);
        if !go {
            continue;
        }
        for cmd in &install.cmds {
            ui::flush();
            if !cmd.run_inherit().is_ok_and(|s| s.success()) {
                ui::error(format!("Could not install {}. Run the command above yourself, then `bui doctor`.", tool.label()));
                break;
            }
        }
        done.extend(shown);
    }
    let still: Vec<Tool> = missing.into_iter().filter(|t| t.probe().is_none()).collect();
    if !still.is_empty() && !done.is_empty() {
        ui::info(format!(
            "Installed, but not visible to this terminal yet: open a new terminal and run {}.",
            ui::code("bui doctor")
        ));
    }
    Ok(still)
}
