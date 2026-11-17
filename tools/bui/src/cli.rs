//! The command line: what `bui --help` shows.

use clap::{Args, Parser, Subcommand, ValueEnum};
use clap_complete::Shell;

use crate::project::{Framework, Language, PackageManager, PyApi};

const EXAMPLES: &str = "\
Examples:
  bui new my-app                          make an app, asking what to use
  bui new my-app -l js --framework react --typescript --pm bun -y
                                          make a React app without any question
  bui dev                                 run it (reloads when you save)
  bui dev -- --release                    anything after -- goes to the tool underneath
  bui build                               make a release build
  bui doctor --fix                        find what is wrong and fix it
  bui update --check                      see what is out of date
  bui update --cli-only                   update bui itself, leave the app's packages

Docs: https://bui.preetham.top   (or run: bui docs)";

#[derive(Parser)]
#[command(
    name = "bui",
    bin_name = "bui",
    version,
    about = "Create, run, check and update BUI apps",
    propagate_version = true,
    after_help = EXAMPLES,
    arg_required_else_help = false
)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Option<Command>,

    /// No animation and no colors (this is automatic without a terminal, in CI, or with NO_COLOR)
    #[arg(long, global = true)]
    pub plain: bool,

    /// Answer every question with its default, and ask nothing
    #[arg(short, long, global = true)]
    pub yes: bool,

    /// Install missing tools (Rust, Bun, uv, ...) without asking
    #[arg(long, global = true)]
    pub install_tools: bool,
}

#[derive(Subcommand)]
pub enum Command {
    /// Make an app in a new folder
    New(NewArgs),
    /// Make an app in this folder
    Init(Choices),
    /// Run the app (reloads when you save, where the language can)
    Dev(RunArgs),
    /// Make a release build
    Build(RunArgs),
    /// Check your setup and the app, and fix what is wrong
    Doctor(DoctorArgs),
    /// Update bui, then the app's BUI packages
    Update(UpdateArgs),
    /// Open the documentation in your browser
    Docs,
    /// Print shell completions (`bui completions bash > ~/.bui-completions.bash`)
    Completions {
        /// bash, zsh, fish, powershell or elvish
        shell: Shell,
    },
}

#[derive(Args)]
pub struct RunArgs {
    /// Arguments for the tool underneath (cargo, your package manager, uv, cmake), after `--`
    #[arg(last = true)]
    pub args: Vec<String>,
}

#[derive(Args)]
pub struct NewArgs {
    /// What to call the app (also its folder)
    pub name: Option<String>,
    #[command(flatten)]
    pub choices: Choices,
}

/// What to make; anything you leave out is asked.
#[derive(Args, Default)]
#[allow(clippy::struct_excessive_bools)] // command-line switches
pub struct Choices {
    /// The language to write it in
    #[arg(short, long, value_enum)]
    pub language: Option<Lang>,
    /// JavaScript: the framework
    #[arg(long, value_enum)]
    pub framework: Option<Framework>,
    /// JavaScript: use TypeScript
    #[arg(long, conflicts_with = "javascript")]
    pub typescript: bool,
    /// JavaScript: use plain JavaScript
    #[arg(long)]
    pub javascript: bool,
    /// JavaScript: the runtime and package manager
    #[arg(long, value_enum)]
    pub pm: Option<PackageManager>,
    /// Python: the style of UI code to write
    #[arg(long, value_enum)]
    pub api: Option<PyApi>,
    /// Do not install dependencies
    #[arg(long)]
    pub no_install: bool,
    /// Do not start a git repository
    #[arg(long)]
    pub no_git: bool,
}

impl Choices {
    /// `--typescript` or `--javascript`, when one was given.
    pub const fn language_choice(&self) -> Option<Language> {
        if self.typescript {
            Some(Language::TypeScript)
        } else if self.javascript {
            Some(Language::JavaScript)
        } else {
            None
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Lang {
    Rust,
    #[value(alias = "javascript", alias = "typescript", alias = "ts")]
    Js,
    Python,
    Lua,
    C,
}

#[derive(Args)]
pub struct DoctorArgs {
    /// Check every language's tools, not only this app's
    #[arg(long)]
    pub all: bool,
    /// Run the fixes without asking
    #[arg(long)]
    pub fix: bool,
    /// Print the results as JSON and change nothing
    #[arg(long)]
    pub json: bool,
}

#[derive(Args)]
pub struct UpdateArgs {
    /// Only say what is out of date
    #[arg(long)]
    pub check: bool,
    /// Update bui itself only, not the packages of the app you are in
    #[arg(long, conflicts_with = "check")]
    pub cli_only: bool,
}
