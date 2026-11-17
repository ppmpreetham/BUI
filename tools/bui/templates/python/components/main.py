"""{{title}}: a window of Blender's own components."""

from blenderui.components import App, Button, Checkbox, ColorSwatch, NumberSlider, Panel


def main() -> None:
    App(
        "{{title}}",
        (420, 420),
        Panel(
            "Transform",
            NumberSlider("Scale", 1.0, min=0, max=10, on_change=print),
        ),
        Panel(
            "Shading",
            Checkbox("Shadows", True),
            ColorSwatch("Color", (0.8, 0.3, 0.1, 1.0)),
        ),
        Button("Apply", on_click=lambda: print("applied")),
    ).run()


if __name__ == "__main__":
    main()
