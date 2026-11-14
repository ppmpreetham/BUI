//! Optional (`Option<T>`) component props: a plain value, an `Option` of one, or a bare `None`.
use blenderui::prelude::*;

#[component]
fn C(a: Option<String>, n: Option<u32>, s: Option<SharedString>) {
    let _ = (a, n, s);
    rsx! { <div /> }
}

#[test]
fn optional_props_take_values_options_and_none() {
    let some: Option<&str> = Some("x");
    let none: Option<String> = None;
    let _ = rsx! { <C a="x" n=3 s="lit" /> };
    let _ = rsx! { <C a={some} n=3 s={some} /> };
    let _ = rsx! { <C a={none} /> };
    let _ = rsx! { <C a={None} n={None} s={None} /> };
    let _ = rsx! { <C /> };
}
