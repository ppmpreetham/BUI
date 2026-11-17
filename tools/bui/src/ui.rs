//! Everything the user sees: the banner, prompts, spinners and progress bars, in one look. Nothing
//! else in `bui` prints directly, so a terminal that cannot draw (a pipe, CI) falls back to plain
//! lines here and only here.

use std::fmt::Display;
use std::io::{self, IsTerminal, Write};
use std::process::exit;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

use anyhow::Result;
use cliclack::{ProgressBar, Theme, ThemeState};
use console::{Style, Term, style, truncate_str};

/// Blender's orange to its blue: the logo's gradient and the accent color of the prompts.
const ORANGE: (u8, u8, u8) = (232, 125, 13);
const BLUE: (u8, u8, u8) = (71, 133, 214);

const LOGO: [&str; 6] = [
    "██████╗ ██╗   ██╗██╗",
    "██╔══██╗██║   ██║██║",
    "██████╔╝██║   ██║██║",
    "██╔══██╗██║   ██║██║",
    "██████╔╝╚██████╔╝██║",
    "╚═════╝  ╚═════╝ ╚═╝",
];

static PLAIN: AtomicBool = AtomicBool::new(false);
static AUTO: AtomicBool = AtomicBool::new(false);
static YES: AtomicBool = AtomicBool::new(false);

/// Can this terminal take prompts and animation: a terminal on both ends, and not CI.
pub fn interactive() -> bool {
    io::stdin().is_terminal() && io::stderr().is_terminal() && std::env::var_os("CI").is_none()
}

/// Sets how to talk: `plain` (no animation), `auto` (answer every question with its default).
pub fn init(plain: bool, auto: bool) {
    let plain = plain || !interactive() || std::env::var_os("NO_COLOR").is_some();
    PLAIN.store(plain, Ordering::Relaxed);
    AUTO.store(auto || !interactive(), Ordering::Relaxed);
    YES.store(auto, Ordering::Relaxed);
    if !plain {
        cliclack::set_theme(BuiTheme);
    }
}

fn plain() -> bool {
    PLAIN.load(Ordering::Relaxed)
}

fn auto() -> bool {
    AUTO.load(Ordering::Relaxed)
}

/// Can the user be asked (a terminal, and no `--yes`)? Anything that changes the machine needs this,
/// or a flag that names it.
pub fn asking() -> bool {
    !auto()
}

/// Without a terminal and without `--yes` nothing can be answered: say so instead of guessing.
fn unanswerable(prompt: &str) -> anyhow::Error {
    anyhow::anyhow!("cannot ask \"{prompt}\" without a terminal: pass the option as a flag, or --yes for the defaults")
}

// ---------------------------------------------------------------------------------------------
// Color

fn lerp(a: (u8, u8, u8), b: (u8, u8, u8), t: f32) -> (u8, u8, u8) {
    let mix = |x: u8, y: u8| (f32::from(x) + (f32::from(y) - f32::from(x)) * t).round() as u8;
    (mix(a.0, b.0), mix(a.1, b.1), mix(a.2, b.2))
}

fn truecolor() -> bool {
    std::env::var("COLORTERM").is_ok_and(|v| v.contains("truecolor") || v.contains("24bit"))
        || std::env::var_os("WT_SESSION").is_some()
        || std::env::var_os("TERM_PROGRAM").is_some()
}

/// `text` in an RGB color (the nearest of 256 where the terminal has no true color).
pub fn paint(text: &str, (r, g, b): (u8, u8, u8)) -> String {
    if !console::colors_enabled_stderr() {
        return text.to_owned();
    }
    if truecolor() {
        return format!("\x1b[38;2;{r};{g};{b}m{text}\x1b[0m");
    }
    let q = |c: u8| (u16::from(c) * 5 / 255) as u8;
    format!("\x1b[38;5;{}m{text}\x1b[0m", 16 + 36 * q(r) + 6 * q(g) + q(b))
}

pub fn accent(text: &str) -> String {
    paint(text, ORANGE)
}

/// A command or a path, as the user would type it.
pub fn code(text: &str) -> String {
    style(text).cyan().to_string()
}

pub fn dim(text: &str) -> String {
    style(text).dim().to_string()
}

struct BuiTheme;

impl Theme for BuiTheme {
    fn bar_color(&self, state: &ThemeState) -> Style {
        match state {
            ThemeState::Active => Style::new().color256(208),
            ThemeState::Cancel => Style::new().red(),
            ThemeState::Submit => Style::new().bright().black(),
            ThemeState::Error(_) => Style::new().yellow(),
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Banner

/// The logo, wiped in left to right with a glint on the edge, then the tagline.
pub fn banner(animate: bool) {
    let version = env!("CARGO_PKG_VERSION");
    if plain() {
        eprintln!("bui {version}");
        return;
    }
    let width = LOGO[0].chars().count();
    let term = Term::stderr();
    let frame = |reveal: usize| {
        let mut out = String::new();
        for line in LOGO {
            for (x, ch) in line.chars().enumerate() {
                if x >= reveal || ch == ' ' {
                    out.push(' ');
                } else if x + 3 >= reveal && reveal < width {
                    out.push_str(&paint(&ch.to_string(), (255, 245, 230)));
                } else {
                    out.push_str(&paint(&ch.to_string(), lerp(ORANGE, BLUE, x as f32 / width as f32)));
                }
            }
            out.push('\n');
        }
        out
    };
    let _ = term.hide_cursor();
    eprintln!();
    if animate {
        for reveal in (0..width).step_by(2) {
            eprint!("{}", frame(reveal));
            thread::sleep(Duration::from_millis(14));
            let _ = term.move_cursor_up(LOGO.len());
        }
    }
    eprint!("{}", frame(width));
    eprintln!("  {}", style(format!("v{version}")).dim());
    eprintln!();
    let _ = term.show_cursor();
}

// ---------------------------------------------------------------------------------------------
// Words

pub fn intro(title: &str) {
    if plain() {
        eprintln!("{title}");
    } else {
        let _ = cliclack::intro(style(format!(" {title} ")).black().on_color256(208));
    }
}

pub fn outro(message: &str) {
    if plain() {
        eprintln!("{message}");
    } else {
        let _ = cliclack::outro(message);
    }
}

pub fn info(message: impl Display) {
    if plain() { eprintln!("  {message}") } else { let _ = cliclack::log::info(message); }
}

pub fn success(message: impl Display) {
    if plain() { eprintln!("✔ {message}") } else { let _ = cliclack::log::success(message); }
}

pub fn warn(message: impl Display) {
    if plain() { eprintln!("! {message}") } else { let _ = cliclack::log::warning(message); }
}

pub fn error(message: impl Display) {
    if plain() { eprintln!("✘ {message}") } else { let _ = cliclack::log::error(message); }
}

/// A titled box.
pub fn note(title: &str, body: &str) {
    if plain() {
        eprintln!("{title}\n{body}");
    } else {
        let _ = cliclack::note(title, body);
    }
}

/// Ends the program as the user cancelled (Esc, Ctrl-C).
pub fn cancelled() -> ! {
    if plain() {
        eprintln!("cancelled");
    } else {
        let _ = cliclack::outro_cancel("Cancelled");
    }
    exit(130)
}

fn answer<T>(r: io::Result<T>) -> Result<T> {
    match r {
        Ok(v) => Ok(v),
        Err(e) if e.kind() == io::ErrorKind::Interrupted => cancelled(),
        Err(e) => Err(e.into()),
    }
}

// ---------------------------------------------------------------------------------------------
// Questions: each has a default for `--yes` and for a terminal that cannot ask, and a `preset`
// (a command-line flag) that skips asking.

pub struct Choice<T> {
    pub value: T,
    pub label: String,
    pub hint: String,
}

pub fn choice<T>(value: T, label: String, hint: impl Into<String>) -> Choice<T> {
    Choice { value, label, hint: hint.into() }
}

pub fn select<T: Clone + Eq>(prompt: &str, choices: &[Choice<T>], preset: Option<&T>) -> Result<T> {
    let pick = preset.and_then(|p| choices.iter().find(|c| &c.value == p));
    if preset.is_none() && auto() && !YES.load(Ordering::Relaxed) {
        return Err(unanswerable(prompt));
    }
    if let Some(c) = pick.or_else(|| auto().then(|| &choices[0])) {
        info(format!("{prompt} {}", style(&c.label).bold()));
        return Ok(c.value.clone());
    }
    let mut q = cliclack::select(prompt);
    for c in choices {
        q = q.item(c.value.clone(), &c.label, &c.hint);
    }
    answer(q.interact())
}

/// Yes or no. With `--yes` the default; without a terminal, no: nothing happens that was not asked for.
pub fn confirm(prompt: &str, default: bool) -> Result<bool> {
    if auto() {
        let answer = default && YES.load(Ordering::Relaxed);
        info(format!("{prompt} {}", style(if answer { "yes" } else { "no" }).bold()));
        return Ok(answer);
    }
    answer(cliclack::confirm(prompt).initial_value(default).interact())
}

/// Text with a default; `valid` returns a message for a bad answer.
pub fn input(prompt: &str, default: &str, valid: impl Fn(&String) -> Option<String> + 'static) -> Result<String> {
    if auto() {
        if !YES.load(Ordering::Relaxed) {
            return Err(unanswerable(prompt));
        }
        info(format!("{prompt} {}", style(default).bold()));
        return Ok(default.to_owned());
    }
    answer(
        cliclack::input(prompt)
            .default_input(default)
            .placeholder(default)
            .validate(move |s: &String| valid(s).map_or(Ok(()), Err))
            .interact(),
    )
}

// ---------------------------------------------------------------------------------------------
// Work that takes a while

/// A spinner with a message that follows what is happening.
pub struct Task {
    bar: Option<ProgressBar>,
}

pub fn task(message: &str) -> Task {
    if plain() {
        eprintln!("… {message}");
        return Task { bar: None };
    }
    let bar = cliclack::spinner();
    bar.start(message);
    Task { bar: Some(bar) }
}

/// A bar for `len` units (a download).
pub fn progress(message: &str, len: u64) -> Task {
    if plain() {
        eprintln!("… {message}");
        return Task { bar: None };
    }
    let bar = cliclack::progress_bar(len).with_download_template();
    bar.start(message);
    Task { bar: Some(bar) }
}

impl Task {
    /// The one-line status next to the spinner (truncated to the terminal).
    pub fn set(&self, message: &str) {
        if let Some(b) = &self.bar {
            let width = Term::stderr().size().1 as usize;
            b.set_message(truncate_str(message, width.saturating_sub(12), "…"));
        }
    }

    pub fn position(&self, done: u64, total: Option<u64>) {
        if let Some(b) = &self.bar {
            if let Some(t) = total {
                b.set_length(t);
            }
            b.set_position(done);
        }
    }

    pub fn done(self, message: &str) {
        match self.bar {
            Some(b) => b.stop(message),
            None => eprintln!("✔ {message}"),
        }
    }

    pub fn fail(self, message: &str) {
        match self.bar {
            Some(b) => b.error(message),
            None => eprintln!("✘ {message}"),
        }
    }
}

/// Runs a command under a spinner that shows what it prints; on failure shows what it said last.
pub fn run(title: &str, cmd: &crate::sys::Cmd) -> Result<bool> {
    let t = task(title);
    let out = cmd.run_live(|line| t.set(&format!("{title}  {}", dim(line))))?;
    if out.ok {
        t.done(title);
    } else {
        t.fail(&format!("{title} failed"));
        for line in &out.tail {
            eprintln!("    {}", dim(line));
        }
        info(format!("Run it yourself to see everything: {}", code(&cmd.display())));
    }
    Ok(out.ok)
}

/// Flushes what was printed (before handing the terminal to another program).
pub fn flush() {
    let _ = io::stderr().flush();
}
