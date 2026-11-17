# bui

The `bui` command: makes, runs, checks and updates BUI apps in Rust, JavaScript, TypeScript, Python,
Lua and C. Docs: [the bui command](https://bui.preetham.top/start/bui-command/).

```sh
curl -LsSf https://bui.preetham.top/install.sh | sh     # Linux and macOS
irm https://bui.preetham.top/install.ps1 | iex           # Windows (PowerShell)
bui new my-app
```

## Build and test

It is its own Cargo workspace and needs no BUI code, so it builds in seconds:

```sh
cd tools/bui
cargo build
cargo clippy --all-targets -- -D warnings
cargo test
```

## How it is organised

- `src/flavours/`: one module per language (`rust`, `js`, `python`, `lua`, `c`). Each implements the
  `Flavour` trait: the tools it needs, the files of a new project, the install steps (always the newest
  versions), how it runs and builds, and its `doctor` checks. A language is a module there and a variant
  of `project::Stack`.
- `templates/`: the files of new projects, laid over each other (`js/common` then `js/react-ts`).
  `{{name}}`, `{{ident}}`, `{{title}}` and `{{tag}}` are filled in; a file named `_gitignore` is written as
  `.gitignore` (a real one here would ignore the templates).
- `src/ui.rs`: everything the user sees (banner, prompts, spinners). Nothing else prints, so a terminal
  without colors or a TTY falls back to plain lines in one place.
- `src/tools.rs`: toolchain detection and the commands that install each tool on each system.
- `src/doctor.rs`, `src/update.rs`, `src/create.rs`, `src/run.rs`: the commands.
- `src/registry.rs`: the newest versions (crates.io, npm, PyPI, and GitHub's release redirect, not its
  rate-limited API).

## Rules it follows

- Nothing is installed or changed without an answer: a prompt, `--fix`, or `--install-tools`. Without a
  terminal and without `--yes` a question stops the program instead of guessing.
- `BUI_RELEASE_TAG` and `BUI_DOWNLOAD_BASE` point `bui update` at another release or a mirror (used by the tests of
  the update path); `BUI_INSTALLER_BASE_URL` does the same for the installers (and `BUI_LATEST_URL`, in `install.ps1`, replaces the page whose
  redirect names the newest release).

## Release

`bui-<target>.tar.gz` / `.zip` (with `.sha256`) are built for six targets by `.github/workflows/cli.yml` when a
release tag is pushed (`tools/release.py`), and the installers and `bui update` read them.
