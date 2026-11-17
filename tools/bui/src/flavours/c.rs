//! C: CMake fetching BUI at a release tag, clang-format and clang-tidy configured.

use std::path::Path;

use anyhow::Result;
use serde::{Deserialize, Serialize};

use super::{File, Flavour, Step, Vars, read, render, step, work_step};
use crate::check::{Check, Fix, Work};
use crate::registry;
use crate::sys::{Cmd, semver};
use crate::tools::Tool;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CApp {}

fn cmake(dir: &Path, args: &[&str]) -> Cmd {
    Cmd::new("cmake", args.iter().copied()).in_dir(dir)
}

/// `text` lower-cased, with the comments (`# ...` and `#[[ ... ]]`) blanked to spaces. Every byte offset stays the
/// same, so a position found here slices `text`. A `#` inside a quoted or bracket argument is not a comment.
fn mask(text: &str) -> String {
    let src = text.as_bytes();
    let mut out = src.to_ascii_lowercase();
    // the length of a bracket opener `[==[` at `i` (its `=` count + 2), if there is one
    let bracket = |i: usize| -> Option<usize> {
        let eq = src[i + 1..].iter().take_while(|&&b| b == b'=').count();
        (src.get(i) == Some(&b'[') && src.get(i + 1 + eq) == Some(&b'[')).then_some(eq + 2)
    };
    // the end of a bracket body that opened with `eq` signs, from `i`
    let close = |i: usize, eq: usize| -> usize {
        let mut shut = vec![b']'];
        shut.extend(std::iter::repeat_n(b'=', eq));
        shut.push(b']');
        src[i..].windows(shut.len()).position(|w| w == shut.as_slice()).map_or(src.len(), |at| i + at + shut.len())
    };
    let mut i = 0;
    while i < src.len() {
        match src[i] {
            b'"' => {
                i += 1;
                while i < src.len() && src[i] != b'"' {
                    i += if src[i] == b'\\' { 2 } else { 1 };
                }
                i += 1;
            }
            b'[' if bracket(i).is_some() => i = close(i + bracket(i).unwrap_or(2), bracket(i).unwrap_or(2) - 2),
            b'#' => {
                let end = match bracket(i + 1) {
                    Some(n) => close(i + 1 + n, n - 2),
                    None => src[i..].iter().position(|&b| b == b'\n').map_or(src.len(), |at| i + at),
                };
                for b in &mut out[i..end.min(src.len())] {
                    if *b != b'\n' && *b != b'\r' {
                        *b = b' ';
                    }
                }
                i = end;
            }
            _ => i += 1,
        }
    }
    String::from_utf8(out).unwrap_or_else(|_| text.to_ascii_lowercase())
}

/// BUI's own `FetchContent_Declare(...)` in a CMakeLists: the text up to where its tag begins, and the
/// tag (`v0.5.1`). Other dependencies' blocks have tags of their own, which are not ours to read or move.
fn bui_pin(text: &str) -> Option<(&str, &str)> {
    const DECLARE: &str = "fetchcontent_declare";
    let lower = mask(text); // a commented-out tag is not the tag
    let mut from = 0;
    while let Some(i) = lower[from..].find(DECLARE) {
        let start = from + i;
        let after = start + DECLARE.len();
        let open = lower[after..].trim_start_matches([' ', '\t']);
        if !open.starts_with('(') {
            from = after;
            continue;
        }
        let body = lower.len() - open.len() + 1;
        let end = lower[body..].find(')').map_or(text.len(), |j| body + j);
        from = end;
        let block = &lower[body..end];
        let name = block.split_whitespace().next().unwrap_or_default();
        if !(matches!(name, "bui" | "blender-ui" | "blenderui") || block.contains("ppmpreetham/bui")) {
            continue;
        }
        let key = lower[start..end].find("git_tag")? + "git_tag".len();
        let rest = &lower[start + key..end]; // found in the masked text, a comment between key and value is blank
        let tag_at = start + key + (rest.len() - rest.trim_start().len());
        let tag_len = lower[tag_at..end].split_whitespace().next()?.len();
        let tag = &text[tag_at..tag_at + tag_len];
        return Some((&text[start..tag_at], tag));
    }
    None
}

/// The release tag `CMakeLists.txt` fetches BUI at.
fn pinned_tag(dir: &Path) -> Option<String> {
    bui_pin(&read(dir, "CMakeLists.txt")?).map(|(_, tag)| tag.to_owned())
}

/// The edit that moves BUI's tag to `to`: it spans from BUI's declaration, so it cannot land on another dependency's tag.
fn move_tag(dir: &Path, to: &str) -> Option<Work> {
    let text = read(dir, "CMakeLists.txt")?;
    let (head, tag) = bui_pin(&text)?;
    Some(Work::Rewrite { path: dir.join("CMakeLists.txt"), from: format!("{head}{tag}"), to: format!("{head}{to}") })
}

impl Flavour for CApp {
    fn label(&self) -> String {
        "C".into()
    }

    fn tools(&self) -> Vec<Tool> {
        vec![Tool::Cc, Tool::Cmake, Tool::Git]
    }

    fn files(&self, vars: &Vars) -> Vec<File> {
        render(&["c"], vars)
    }

    fn deps(&self, dir: &Path) -> Vec<Step> {
        vec![step(
            "Fetching and building BUI once (a few minutes now, seconds after)",
            cmake(dir, &["--workflow", "--preset", "default"]),
        )]
    }

    fn dev(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>> {
        let exe = dir.join("build/bin").join(if cfg!(windows) { "app.exe" } else { "app" });
        Ok(vec![
            cmake(dir, &["--preset", "default"]),
            cmake(dir, &["--build", "--preset", "default"]),
            Cmd::new(&exe.to_string_lossy(), Vec::<String>::new()).in_dir(dir).with_args(args),
        ])
    }

    fn build(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>> {
        Ok(vec![cmake(dir, &["--preset", "default"]), cmake(dir, &["--build", "--preset", "release"]).with_args(args)])
    }

    fn checks(&self, dir: &Path) -> Vec<Check> {
        let Some(have) = pinned_tag(dir) else {
            return vec![Check::fail("BUI version", "CMakeLists.txt has no BUI FetchContent_Declare with a GIT_TAG", None)];
        };
        let latest = registry::bui_version().map(|v| format!("v{v}"));
        match latest {
            Some(l) if semver(&l) > semver(&have) => {
                let fix = move_tag(dir, &l).map(|w| Fix::edit(format!("moves BUI's GIT_TAG to {l}"), vec![w]));
                vec![Check::warn("BUI release", format!("{have}, newest is {l}"), fix)]
            }
            Some(_) => vec![Check::ok("BUI release", format!("{have} (newest)"))],
            None => vec![Check::ok("BUI release", format!("{have} (could not look up the newest)"))],
        }
    }

    fn update(&self, dir: &Path) -> Vec<Step> {
        let Some(latest) = registry::bui_version().map(|v| format!("v{v}")) else {
            return Vec::new();
        };
        move_tag(dir, &latest).map(|work| work_step("Moving BUI to the newest release", work)).into_iter().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TWO: &str = "include(FetchContent)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG v0.5.1)
fetchcontent_declare(
  bui
  GIT_REPOSITORY https://github.com/ppmpreetham/BUI.git
  GIT_TAG v0.5.1
  GIT_SHALLOW TRUE)
";

    #[test]
    fn the_tag_is_read_from_buis_own_declaration() {
        let (_, tag) = bui_pin(TWO).unwrap();
        assert_eq!(tag, "v0.5.1");
        let other = TWO.replace("GIT_TAG v0.5.1
  GIT_SHALLOW", "GIT_TAG v0.6.0
  GIT_SHALLOW");
        assert_eq!(bui_pin(&other).unwrap().1, "v0.6.0", "not the first dependency's");
        assert!(bui_pin("FetchContent_Declare(fmt GIT_TAG 1)").is_none());
    }

    #[test]
    fn moving_the_tag_leaves_other_dependencies_alone() {
        let dir = std::env::temp_dir().join(format!("bui-c-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("CMakeLists.txt"), TWO).unwrap();
        let work = move_tag(&dir, "v0.9.0").unwrap();
        assert!(work.perform("test").unwrap());
        let text = std::fs::read_to_string(dir.join("CMakeLists.txt")).unwrap();
        assert_eq!(text, TWO.replacen("GIT_TAG v0.5.1
  GIT_SHALLOW", "GIT_TAG v0.9.0
  GIT_SHALLOW", 1));
        assert_eq!(pinned_tag(&dir).as_deref(), Some("v0.9.0"));
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn spaces_before_the_parenthesis_and_comments_are_cmake_syntax() {
        let text = "FetchContent_Declare (bui # the \u{e9}tag below\n  GIT_REPOSITORY https://github.com/ppmpreetham/BUI.git\n  # GIT_TAG v0.4.0\n  GIT_TAG v0.5.1)\n";
        let (head, tag) = bui_pin(text).unwrap();
        assert_eq!(tag, "v0.5.1");
        assert!(head.ends_with("# GIT_TAG v0.4.0\n  GIT_TAG "));
    }

    #[test]
    fn a_hash_in_a_quoted_or_bracket_argument_is_not_a_comment() {
        let text = "FetchContent_Declare(bui SOURCE_DIR \"bui#src \\\" #x\" GIT_TAG v0.5.1)\n";
        assert_eq!(bui_pin(text).unwrap().1, "v0.5.1");
        let text = "FetchContent_Declare(bui #[==[ GIT_TAG v0.1 ]] ]==] GIT_TAG v0.5.1 #[[ x\n GIT_TAG v0.2 ]])\n";
        assert_eq!(bui_pin(text).unwrap().1, "v0.5.1");
        let text = "FetchContent_Declare(bui SOURCE_DIR [[a#b]] GIT_TAG v0.5.1)\n";
        assert_eq!(bui_pin(text).unwrap().1, "v0.5.1");
    }

    #[test]
    fn a_comment_between_the_key_and_the_value_is_skipped() {
        let text = "FetchContent_Declare (bui\n  GIT_TAG # pinned \u{e9}\n    v0.5.1)\n";
        let (head, tag) = bui_pin(text).unwrap();
        assert_eq!(tag, "v0.5.1");
        assert!(head.ends_with("pinned \u{e9}\n    "));
    }
}
