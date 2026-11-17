"""{{title}}: tkinter-style code. Only the import differs from the standard library."""

import blenderui.tksyntax as tk
from blenderui.tksyntax import messagebox, ttk


def main() -> None:
    root = tk.Tk()
    root.title("{{title}}")
    name = tk.StringVar(value="Cube")
    ttk.Entry(root, textvariable=name).pack(fill="x", padx=8, pady=8)
    ttk.Button(
        root,
        text="Greet",
        command=lambda: messagebox.showinfo("Hi", "Hello " + name.get()),
    ).pack()
    root.mainloop()


if __name__ == "__main__":
    main()
