//! `#[component] fn x(Props { .. }: Props)`: a `#[derive(Props)]` struct as the component's props.
use blenderui::prelude::*;

#[derive(Props)]
struct ChatProps {
    title: SharedString,
    unread: bool,
    note: Option<SharedString>,
}

#[component]
fn chat(ChatProps { title, unread, note }: ChatProps) {
    let note = note.unwrap_or_default();
    rsx! {
        <div class="flex" data-note={note.to_string()}>
            {title.to_string()}
            {unread.then(|| rsx! { <span>"new"</span> })}
        </div>
    }
}

#[test]
fn struct_props_component_builds() {
    let _ = rsx! { <chat title="Alice" unread={true} /> };
    let _ = rsx! { <chat title="Bob" unread={false} note="hi" /> };
}

#[test]
fn spread_props_value_is_the_element() {
    let props = || ChatProps { title: "Eve".into(), unread: false, note: None };
    let _ = rsx! { <div><chat {...props()} /><chat {...props()} /></div> };
}
