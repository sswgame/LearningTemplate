"""
Scripts/generate/GenerateDocs.py

Doxygen 문서 생성.
"""

from __future__ import annotations

import subprocess
import sys
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import GetProjectRoot


def GenerateDocs() -> None:
	root_dir = GetProjectRoot()
	doxyfile_path = root_dir / "Doxyfile"
	docs_dir = root_dir / "Docs" / "Doxygen" / "html"
	index_html = docs_dir / "index.html"

	if not doxyfile_path.exists():
		print(f"Warning: Doxyfile not found at {doxyfile_path}; skipping documentation generation.")
		sys.exit(0)

	print("Running Doxygen...")
	try:
		subprocess.run(["doxygen", "Doxyfile"], cwd=root_dir, check=True)
	except FileNotFoundError:
		print(
			"Error: 'doxygen' command not found. Please ensure Doxygen is installed and added to your system PATH."
		)
		sys.exit(1)
	except subprocess.CalledProcessError as e:
		print(f"Error: Doxygen failed with exit code {e.returncode}")
		sys.exit(1)

	print("Doxygen documentation generated successfully.")
	if index_html.exists():
		print(f"Opening {index_html} in the default browser...")
		webbrowser.open(f"file://{index_html.as_posix()}")
	else:
		print(f"Warning: Could not find {index_html} after generating documentation.")


if __name__ == "__main__":
	GenerateDocs()
