"""PyInstaller entry point for the standalone semantic Worker."""
import os
import sys

from san14_worker.__main__ import main

if __name__ == "__main__":
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w", encoding="utf-8")
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w", encoding="utf-8")
    main()
