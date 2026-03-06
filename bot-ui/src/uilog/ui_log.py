from __future__ import annotations

import datetime as dt
import os
import tkinter as tk
from tkinter import scrolledtext


class UILog:
    def __init__(self, text_widget: scrolledtext.ScrolledText, file_path: str = r"D:\logs\controller.log") -> None:
        self.widget = text_widget
        self.file_path = file_path
        self._ensure_log_dir()

    def write(self, message: str) -> None:
        now = dt.datetime.now().strftime("%H:%M:%S")
        line = f"[{now}] {message}"
        self.widget.configure(state=tk.NORMAL)
        self.widget.insert(tk.END, line + "\n")
        self.widget.see(tk.END)
        self.widget.configure(state=tk.DISABLED)
        self._write_file_line(line)

    def _ensure_log_dir(self) -> None:
        try:
            directory = os.path.dirname(self.file_path)
            if directory:
                os.makedirs(directory, exist_ok=True)
        except OSError:
            pass

    def _write_file_line(self, line: str) -> None:
        try:
            with open(self.file_path, "a", encoding="utf-8") as handle:
                handle.write(line + "\n")
        except OSError:
            # Keep UI usable even if file logging fails.
            pass
