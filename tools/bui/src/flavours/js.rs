//! JavaScript and TypeScript: React, Svelte or BUI's own DOM-like API, on Bun, npm or pnpm, with
//! Biome for lint and format and TypeScript's strict mode.

use std::path::Path;

use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};
use serde_json::json;

use super::{Adopted, File, Flavour, Step, Vars, read, render, step};
use crate::check::{self, Check, Fix};
use crate::project::{Framework, Language, PackageManager};
use crate::registry;
use crate::sys::Cmd;
use crate::tools::Tool;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct JsApp {
    pub framework: Framework,
    pub language: Language,
    pub pm: PackageManager,
}

impl PackageManager {
    const fn bin(self) -> &'static str {
        match self {
            Self::Bun => "bun",
            Self::Npm => "npm",
            Self::Pnpm => "pnpm",
        }
    }

    /// `bun add` / `npm install` / `pnpm add`, with the packages at their newest versions.
    fn add(self, dir: &Path, dev: bool, packages: &[&str]) -> Cmd {
        let mut args: Vec<&str> = vec![match self {
            Self::Npm => "install",
            Self::Bun | Self::Pnpm => "add",
        }];
        if dev {
            args.push(if self == Self::Npm { "--save-dev" } else { "--dev" });
        }
        args.extend_from_slice(packages);
        Cmd::new(self.bin(), args).in_dir(dir)
    }

    /// Runs a `package.json` script; npm and pnpm need `--` before the user's arguments.
    fn run(self, dir: &Path, script: &str, args: &[String]) -> Cmd {
        let mut cmd = Cmd::new(self.bin(), ["run", script]).in_dir(dir);
        if !args.is_empty() && self != Self::Bun {
            cmd = cmd.with_args(&["--".to_owned()]);
        }
        cmd.with_args(args)
    }

    /// Everything the lock file names, at the newest version the ranges allow.
    fn update(self, dir: &Path) -> Cmd {
        Cmd::new(self.bin(), ["update"]).in_dir(dir)
    }
}

impl JsApp {
    const fn typed(&self) -> bool {
        matches!(self.language, Language::TypeScript)
    }

    /// The script the app starts from.
    const fn entry(&self) -> &'static str {
        match (self.framework, self.language) {
            (Framework::React, Language::TypeScript) => "src/main.tsx",
            (Framework::React, Language::JavaScript) => "src/main.jsx",
            (Framework::Svelte, _) | (Framework::Vanilla, Language::JavaScript) => "src/main.js",
            (Framework::Vanilla, Language::TypeScript) => "src/main.ts",
        }
    }

    /// Run by `tsx` (TypeScript and JSX on Node): in CommonJS mode, as Node cannot import the named
    /// exports of BUI's CommonJS packages from an ES module.
    fn uses_tsx(&self) -> bool {
        self.pm != PackageManager::Bun && self.framework != Framework::Svelte && (self.typed() || self.framework == Framework::React)
    }

    /// The command that runs the entry, as `bun`, `tsx` or `node` do, with the Svelte loader where needed.
    fn runner(&self, watch: bool) -> String {
        let entry = self.entry();
        let flag = |on: &'static str| if watch { on } else { "" };
        match self.framework {
            // @blender-ui/svelte's loader does not work under Bun (its compiler call fails there): Node runs it
            Framework::Svelte => format!("node --import @blender-ui/svelte/register {}{entry}", flag("--watch ")),
            _ if self.pm == PackageManager::Bun => format!("bun {}{entry}", flag("--watch ")),
            _ if self.uses_tsx() => format!("tsx {}{entry}", flag("watch ")),
            _ => format!("node {}{entry}", flag("--watch ")),
        }
    }

    /// The scripts `bui dev`, `bui build` and the formatting step run through the package manager.
    fn scripts(&self) -> serde_json::Map<String, serde_json::Value> {
        let mut scripts = serde_json::Map::new();
        let mut put = |k: &str, v: String| {
            scripts.insert(k.into(), json!(v));
        };
        put("dev", self.runner(true));
        put("start", self.runner(false));
        put("build", format!("bui build {}", self.entry()));
        if self.typed() {
            put("typecheck", "tsc --noEmit".into());
        }
        put("lint", "biome check .".into());
        put("format", "biome format --write .".into());
        put("fix", "biome check --write .".into());
        scripts
    }

    fn package_json(&self, name: &str) -> String {
        let mut doc = json!({
            "name": name,
            "version": "0.1.0",
            "private": true,
            "scripts": self.scripts(),
        });
        if !self.uses_tsx() {
            doc["type"] = json!("module");
        }
        format!("{}\n", serde_json::to_string_pretty(&doc).unwrap_or_default())
    }

    fn tsconfig(&self) -> String {
        let types = if self.pm == PackageManager::Bun { "bun" } else { "node" };
        let mut options = json!({
            "target": "ES2023",
            "module": "ESNext",
            "moduleResolution": "Bundler",
            "strict": true,
            "noUncheckedIndexedAccess": true,
            "verbatimModuleSyntax": true,
            "isolatedModules": true,
            "skipLibCheck": true,
            "noEmit": true,
            "types": [types],
        });
        if self.framework == Framework::React {
            options["jsx"] = json!("react-jsx");
            options["jsxImportSource"] = json!("@blender-ui/react");
        }
        format!("{}\n", serde_json::to_string_pretty(&json!({ "compilerOptions": options, "include": ["src"] })).unwrap_or_default())
    }

    fn packages(&self) -> (Vec<&'static str>, Vec<&'static str>) {
        let mut prod = vec!["@blender-ui/native"];
        match self.framework {
            Framework::React => prod.extend(["@blender-ui/react", "react"]),
            Framework::Svelte => prod.extend(["@blender-ui/svelte", "svelte"]),
            Framework::Vanilla => {}
        }
        let mut dev = vec!["@biomejs/biome"];
        if self.typed() {
            dev.extend(["typescript", if self.pm == PackageManager::Bun { "@types/bun" } else { "@types/node" }]);
            if self.framework == Framework::React {
                dev.push("@types/react");
            }
        }
        if self.uses_tsx() {
            dev.push("tsx");
        }
        (prod, dev)
    }
}

impl Flavour for JsApp {
    fn label(&self) -> String {
        let fw = match self.framework {
            Framework::React => "React",
            Framework::Svelte => "Svelte",
            Framework::Vanilla => "plain DOM",
        };
        let lang = if self.typed() { "TypeScript" } else { "JavaScript" };
        format!("{fw} + {lang} on {}", self.pm.bin())
    }

    fn tools(&self) -> Vec<Tool> {
        let mut tools = match self.pm {
            PackageManager::Bun => vec![Tool::Bun],
            PackageManager::Npm => vec![Tool::Node],
            PackageManager::Pnpm => vec![Tool::Node, Tool::Pnpm],
        };
        if self.framework == Framework::Svelte && !tools.contains(&Tool::Node) {
            tools.push(Tool::Node);
        }
        tools
    }

    /// `@blender-ui/native`'s `bui` bundles with Bun (bindings/node/bundle.js has no other path), whatever runs the app.
    fn build_tools(&self) -> Vec<Tool> {
        let mut tools = self.tools();
        if !tools.contains(&Tool::Bun) {
            tools.push(Tool::Bun);
        }
        tools
    }

    /// An existing `package.json` keeps everything it has; the scripts the commands run are added when absent.
    fn adopt(&self, dir: &Path) -> Result<Vec<Adopted>> {
        let path = dir.join("package.json");
        let text = match std::fs::read_to_string(&path) {
            Ok(text) => text,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
            Err(e) => return Err(e).with_context(|| format!("reading {}: fix it first, then run `bui init` again", path.display())),
        };
        let mut doc: serde_json::Value = serde_json::from_str(&text).with_context(|| format!("{} is not valid JSON: fix it first, then run `bui init` again", path.display()))?;
        let Some(root) = doc.as_object_mut() else {
            bail!("{} is not a JSON object: fix it first, then run `bui init` again", path.display());
        };
        if self.uses_tsx() && root.get("type").and_then(serde_json::Value::as_str) == Some("module") {
            bail!(
                "{} has \"type\": \"module\", and `tsx` would then load the entry as an ES module, where Node cannot import the named exports of the CommonJS @blender-ui/react that way: remove \"type\" (or pick JavaScript, or another framework), then run `bui init` again",
                path.display()
            );
        }
        let scripts = root.entry("scripts").or_insert_with(|| json!({}));
        let Some(scripts) = scripts.as_object_mut() else {
            bail!("\"scripts\" in {} is not an object: fix it first, then run `bui init` again", path.display());
        };
        let mut added = Vec::new();
        for (name, command) in self.scripts() {
            if !scripts.contains_key(&name) {
                scripts.insert(name.clone(), command);
                added.push(name);
            }
        }
        let note = if added.is_empty() {
            "has every script bui runs; left as it was".to_owned()
        } else {
            std::fs::write(&path, format!("{}
", serde_json::to_string_pretty(&doc)?)).with_context(|| format!("writing {}", path.display()))?;
            format!("kept as it was, and added the scripts {}", added.join(", "))
        };
        Ok(vec![Adopted { path: "package.json", note }])
    }

    fn files(&self, vars: &Vars) -> Vec<File> {
        let flavour = match (self.framework, self.language) {
            (Framework::React, Language::TypeScript) => "js/react-ts",
            (Framework::React, Language::JavaScript) => "js/react-js",
            (Framework::Svelte, _) => "js/svelte",
            (Framework::Vanilla, Language::TypeScript) => "js/vanilla-ts",
            (Framework::Vanilla, Language::JavaScript) => "js/vanilla-js",
        };
        let mut files = render(&["js/common", flavour], vars);
        files.push(File { path: "package.json".into(), body: self.package_json(&vars.name) });
        if self.typed() {
            files.push(File { path: "tsconfig.json".into(), body: self.tsconfig() });
        }
        files
    }

    fn deps(&self, dir: &Path) -> Vec<Step> {
        let (prod, dev) = self.packages();
        vec![
            step("Adding BUI (newest)", self.pm.add(dir, false, &prod)),
            step("Adding TypeScript, Biome and the rest (newest)", self.pm.add(dir, true, &dev)),
            // the generated files in Biome's own style, so `lint` starts clean
            step("Formatting the new files", self.pm.run(dir, "fix", &[])),
        ]
    }

    fn dev(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>> {
        Ok(vec![self.pm.run(dir, "dev", args)])
    }

    fn build(&self, dir: &Path, args: &[String]) -> Result<Vec<Cmd>> {
        Ok(vec![self.pm.run(dir, "build", args)])
    }

    fn checks(&self, dir: &Path) -> Vec<Check> {
        let install = Fix::run("installs the packages", vec![Cmd::new(self.pm.bin(), ["install"]).in_dir(dir)]);
        if !dir.join("node_modules").is_dir() {
            return vec![Check::fail("node_modules", "packages are not installed", Some(install))];
        }
        let manifest = read(dir, "node_modules/@blender-ui/native/package.json");
        let have = manifest.and_then(|m| serde_json::from_str::<serde_json::Value>(&m).ok()).and_then(|v| v["version"].as_str().map(str::to_owned));
        let Some(have) = have else {
            return vec![Check::fail("@blender-ui/native", "not installed", Some(install))];
        };
        let fix = Fix::run("updates the packages to the newest", vec![self.pm.update(dir)]);
        vec![check::newest("@blender-ui/native", &have, registry::npm_version("@blender-ui/native").as_deref(), fix)]
    }

    fn update(&self, dir: &Path) -> Vec<Step> {
        vec![step("Updating packages", self.pm.update(dir))]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn app() -> JsApp {
        JsApp { framework: Framework::Vanilla, language: Language::JavaScript, pm: PackageManager::Npm }
    }

    fn scratch(tag: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("bui-js-{tag}-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn an_existing_manifest_gets_only_the_missing_scripts() {
        let dir = scratch("adopt");
        let manifest = r#"{"name":"mine","zeta":1,"scripts":{"dev":"vite","test":"vitest"},"alpha":2}"#;
        std::fs::write(dir.join("package.json"), manifest).unwrap();
        let adopted = app().adopt(&dir).unwrap();
        assert_eq!(adopted.len(), 1);
        let doc: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("package.json")).unwrap()).unwrap();
        assert_eq!(doc["scripts"]["dev"], "vite", "their script is kept");
        assert_eq!(doc["scripts"]["test"], "vitest");
        assert_eq!(doc["scripts"]["build"], "bui build src/main.js");
        assert_eq!(doc["scripts"]["fix"], "biome check --write .");
        let keys: Vec<&String> = doc.as_object().unwrap().keys().collect();
        assert_eq!(keys, ["name", "zeta", "scripts", "alpha"], "their order is kept");
        let again = std::fs::read_to_string(dir.join("package.json")).unwrap();
        assert!(app().adopt(&dir).unwrap()[0].note.contains("left as it was"));
        assert_eq!(again, std::fs::read_to_string(dir.join("package.json")).unwrap(), "a second pass changes nothing");
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn a_manifest_that_cannot_be_read_stops_init() {
        let dir = scratch("broken");
        std::fs::write(dir.join("package.json"), "{ not json").unwrap();
        assert!(app().adopt(&dir).is_err());
        assert_eq!(std::fs::read_to_string(dir.join("package.json")).unwrap(), "{ not json", "it is not touched");
        std::fs::write(dir.join("package.json"), r#"{"scripts": []}"#).unwrap();
        assert!(app().adopt(&dir).is_err());
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn unreadable_manifests_and_module_type_with_tsx_stop_init() {
        let dir = scratch("stop");
        std::fs::write(dir.join("package.json"), [0xff, 0xfe, 0x7b]).unwrap();
        assert!(app().adopt(&dir).is_err(), "invalid UTF-8 is an error, not an absent file");
        let tsx = JsApp { framework: Framework::React, language: Language::TypeScript, pm: PackageManager::Npm };
        std::fs::write(dir.join("package.json"), r#"{"type":"module"}"#).unwrap();
        assert!(tsx.adopt(&dir).is_err(), "tsx cannot import the CommonJS react package as ESM");
        assert!(app().adopt(&dir).is_ok(), "plain JavaScript can");
        assert_eq!(app().adopt(&scratch("none")).unwrap().len(), 0, "no manifest, nothing adopted");
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn building_needs_bun_whatever_runs_the_app() {
        for pm in [PackageManager::Npm, PackageManager::Pnpm, PackageManager::Bun] {
            let tools = JsApp { pm, ..app() }.build_tools();
            assert_eq!(tools.iter().filter(|t| **t == Tool::Bun).count(), 1);
        }
        assert!(!app().tools().contains(&Tool::Bun), "running does not");
    }
}
