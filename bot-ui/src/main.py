from __future__ import annotations

import tkinter as tk

from app import BotUIApp


def main() -> None:
    root = tk.Tk()
    _app = BotUIApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
