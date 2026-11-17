//! `bui`: create, run, check and update BUI apps.

mod check;
mod cli;
mod create;
mod doctor;
mod flavours;
mod home;
mod project;
mod registry;
mod run;
mod sys;
mod tools;
mod ui;
mod update;

use std::process::ExitCode;

use anyhow::Result;
use clap::{CommandFactory, Parser};

use cli::{Cli, Command};

fn main() -> ExitCode {
    let cli = Cli::parse();
    ui::init(cli.plain, cli.yes);
    if cli.install_tools {
        tools::install_without_asking();
    }
    match dispatch(cli) {
        Ok(code) => code,
        Err(e) => {
            ui::error(format!("{e:#}"));
            if project::find(std::path::Path::new(".")).is_some() {
                ui::info(format!("Run {} to check your setup.", ui::code("bui doctor")));
            }
            ExitCode::FAILURE
        }
    }
}

fn dispatch(cli: Cli) -> Result<ExitCode> {
    let ok = |r: Result<()>| r.map(|()| ExitCode::SUCCESS);
    match cli.command {
        Some(Command::New(args)) => ok(create::new(args)),
        Some(Command::Init(choices)) => ok(create::init(&choices)),
        Some(Command::Dev(a)) => run::dev(&a.args),
        Some(Command::Build(a)) => run::build(&a.args),
        Some(Command::Docs) => Ok(run::docs()),
        Some(Command::Doctor(args)) => doctor::run(&args),
        Some(Command::Update(args)) => ok(update::run(&args)),
        Some(Command::Completions { shell }) => {
            clap_complete::generate(shell, &mut Cli::command(), "bui", &mut std::io::stdout());
            Ok(ExitCode::SUCCESS)
        }
        None => {
            home::show();
            Ok(ExitCode::SUCCESS)
        }
    }
}
