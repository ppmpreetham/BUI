//! What `bui` prints with no command: the app you are in, and every command with its options.

use std::path::Path;

use crate::{project, ui};

/// (what to type, what it does, its options as `(flag, meaning)`)
type Entry = (&'static str, &'static str, &'static [(&'static str, &'static str)]);

const COMMANDS: &[Entry] = &[
    (
        "bui new [NAME]",
        "Make an app in a new folder",
        &[
            ("-l, --language <rust|js|python|lua|c>", "the language"),
            ("--framework <react|svelte|vanilla>", "JavaScript"),
            ("--typescript | --javascript", "JavaScript"),
            ("--pm <bun|npm|pnpm>", "JavaScript: runtime and package manager"),
            ("--api <components|qt|tk>", "Python: the style of UI code"),
            ("--no-install", "do not install packages"),
            ("--no-git", "do not start a git repository"),
        ],
    ),
    ("bui init", "Make an app in this folder (same options as new)", &[]),
    ("bui dev [-- ARGS]", "Run the app (reloads when you save)", &[("-- ARGS", "given to the tool underneath")]),
    ("bui build [-- ARGS]", "Make a release build", &[("-- ARGS", "given to the tool underneath")]),
    (
        "bui doctor",
        "Check your setup and fix what is wrong",
        &[("--fix", "run the fixes without asking"), ("--all", "check every language's tools"), ("--json", "print the results, change nothing")],
    ),
    ("bui update", "Update bui, then this app's BUI packages", &[("--check", "only say what is out of date")]),
    ("bui docs", "Open the documentation", &[]),
    ("bui completions <SHELL>", "Shell completions", &[("<SHELL>", "bash, zsh, fish, powershell or elvish")]),
];

const EVERYWHERE: &[(&str, &str)] = &[
    ("--plain", "no animation or color (automatic in CI and pipes)"),
    ("-y, --yes", "answer every question with its default"),
    ("--install-tools", "install missing tools without asking"),
    ("-h, --help", "help for any command"),
    ("-V, --version", "the version"),
];

fn render(entries: &[Entry]) -> String {
    let mut lines = Vec::new();
    for (usage, what, options) in entries {
        lines.push(format!("{}  {}", ui::code(&format!("{usage:<25}")), what));
        for (flag, meaning) in *options {
            let text = if meaning.is_empty() { (*flag).to_owned() } else { format!("{flag:<40}{meaning}") };
            lines.push(format!("    {}", ui::dim(&text)));
        }
    }
    lines.join("\n")
}

pub fn show() {
    ui::banner(true);
    if let Some(p) = project::find(Path::new(".")) {
        ui::note("This app", &format!("{}  {}", p.meta.name, ui::dim(&p.meta.stack.app().label())));
    }
    ui::note("Commands", &render(COMMANDS));
    let everywhere: Vec<String> = EVERYWHERE.iter().map(|(f, m)| format!("{}  {}", ui::code(&format!("{f:<16}")), m)).collect();
    ui::note("Options for every command", &everywhere.join("\n"));
    ui::outro(&format!("New here? Run {}. Docs: {}", ui::code("bui new"), ui::code("https://bui.preetham.top")));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_command_is_listed() {
        let text = console::strip_ansi_codes(&render(COMMANDS)).into_owned();
        for word in ["bui new", "bui init", "bui dev", "bui build", "bui doctor", "bui update", "bui docs", "bui completions", "--fix", "--language"] {
            assert!(text.contains(word), "{word} missing");
        }
    }
}
