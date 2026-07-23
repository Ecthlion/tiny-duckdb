import os
import re
import sys


def main() -> int:
	repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

	cpp_re = re.compile(
	    r"(?P<indent>[ \t]*)// \[SOLUTION BEGIN (?P<task>L\d+\.T\d+(?:-T\d+)?)\]\n.*?\n[ \t]*// \[SOLUTION END\]",
	    re.DOTALL,
	)
	py_re = re.compile(
	    r"(?P<indent>[ \t]*)# \[SOLUTION BEGIN (?P<task>L\d+\.T\d+(?:-T\d+)?)\]\n.*?\n[ \t]*# \[SOLUTION END\]",
	    re.DOTALL,
	)

	def cpp_stub(match: re.Match[str]) -> str:
		indent = match.group("indent")
		task = match.group("task")
		return (
		    f'{indent}// TODO({task}): implement this (see the corresponding docs/labN.md)\n'
		    f'{indent}throw NotImplementedException("task {task} not implemented yet");'
		)

	def py_stub(match: re.Match[str]) -> str:
		indent = match.group("indent")
		task = match.group("task")
		return (
		    f"{indent}# TODO({task}): implement this (see the lab handout)\n"
		    f'{indent}raise NotImplementedError("task {task} not implemented yet")'
		)

	skipped_dirs = {".git", "build", "cmake-build", ".venv", "__pycache__", ".pytest_cache"}

	count = 0
	for dirpath, dirnames, filenames in os.walk(repo_root):
		dirnames[:] = [d for d in dirnames if d not in skipped_dirs]
		for filename in filenames:
			path = os.path.join(dirpath, filename)
			if filename.endswith((".cpp", ".hpp", ".h")):
				pattern, stub = cpp_re, cpp_stub
			elif filename.endswith(".py"):
				pattern, stub = py_re, py_stub
			else:
				continue

			with open(path, "r", encoding="utf-8") as f:
				source = f.read()
			source, n = pattern.subn(stub, source)
			if n > 0:
				count += n
				with open(path, "w", encoding="utf-8") as f:
					f.write(source)

	print(f"stripped {count} solution blocks")
	return 0


if __name__ == "__main__":
	sys.exit(main())
