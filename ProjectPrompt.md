# Demonstration project

Create a C++20 CMake project for a utility named `normalize-lines`. Put argument handling and line normalization in separate translation units. Accept an input path and an output path. Read the input as bytes, convert CRLF and lone CR to LF, and preserve every other byte including NUL. Write the result to the output path. Report unreadable input and unwritable output with a diagnostic and a nonzero exit status. Reject identical input and output paths before opening the output for writing.

Include CTest fixtures for mixed line endings, an empty file, a file containing NUL, an unreadable input path, and identical paths. Verify output by exact byte comparison. Configure, build, and test Debug and Release separately.
