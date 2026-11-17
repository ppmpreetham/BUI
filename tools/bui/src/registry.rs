//! The newest versions of things, from the registries. Every lookup can fail (offline): the answer is
//! `None` and the caller carries on.

use std::time::Duration;

use serde_json::Value;

const REPO: &str = "ppmpreetham/BUI";

fn get(url: &str) -> Option<Value> {
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(6)))
        .user_agent(concat!("bui-cli/", env!("CARGO_PKG_VERSION")))
        .build()
        .into();
    agent.get(url).call().ok()?.body_mut().read_json().ok()
}

fn text(v: &Value, path: &[&str]) -> Option<String> {
    path.iter().try_fold(v, |v, k| v.get(k))?.as_str().map(str::to_owned)
}

pub fn crate_version(name: &str) -> Option<String> {
    text(&get(&format!("https://crates.io/api/v1/crates/{name}"))?, &["crate", "max_version"])
}

pub fn npm_version(package: &str) -> Option<String> {
    text(&get(&format!("https://registry.npmjs.org/{package}/latest"))?, &["version"])
}

pub fn pypi_version(package: &str) -> Option<String> {
    text(&get(&format!("https://pypi.org/pypi/{package}/json"))?, &["info", "version"])
}

/// The tag of the newest GitHub release of the `bui` program (`v0.5.1`). It follows the page's redirect
/// (`/releases/latest` -> `/releases/tag/v0.5.1`): the API would rate-limit a tool that runs on every machine.
pub fn release_tag() -> Option<String> {
    // for tests and mirrors
    if let Ok(tag) = std::env::var("BUI_RELEASE_TAG") {
        return Some(tag);
    }
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(6)))
        .max_redirects(0)
        .max_redirects_will_error(false)
        .user_agent(concat!("bui-cli/", env!("CARGO_PKG_VERSION")))
        .build()
        .into();
    let response = agent.get(&format!("https://github.com/{REPO}/releases/latest")).call().ok()?;
    let location = response.headers().get("location")?.to_str().ok()?;
    location.rsplit_once("/tag/").map(|(_, tag)| tag.to_owned())
}

/// The newest BUI the language packages carry (the crate is published with every release).
pub fn bui_version() -> Option<String> {
    crate_version("blenderui")
}

/// Where a release's file is.
pub fn download_url(tag: &str, asset: &str) -> String {
    if let Ok(base) = std::env::var("BUI_DOWNLOAD_BASE") {
        return format!("{}/{asset}", base.trim_end_matches('/'));
    }
    format!("https://github.com/{REPO}/releases/download/{tag}/{asset}")
}

/// A file from the release as bytes (the archive of a new `bui`), with progress reported to `on_chunk`.
pub fn download(url: &str, mut on_chunk: impl FnMut(u64, Option<u64>)) -> Option<Vec<u8>> {
    use std::io::Read;
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(120)))
        .user_agent(concat!("bui-cli/", env!("CARGO_PKG_VERSION")))
        .build()
        .into();
    let mut response = agent.get(url).call().ok()?;
    let total = response.headers().get("content-length").and_then(|v| v.to_str().ok()?.parse().ok());
    let mut reader = response.body_mut().as_reader();
    let mut bytes = Vec::with_capacity(total.map_or(1 << 20, |t: u64| t as usize));
    let mut buf = vec![0u8; 32 * 1024].into_boxed_slice();
    loop {
        let n = reader.read(&mut buf).ok()?;
        if n == 0 {
            return Some(bytes);
        }
        bytes.extend_from_slice(&buf[..n]);
        on_chunk(bytes.len() as u64, total);
    }
}
