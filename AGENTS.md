# AGENT Instructions

## Scope
These guidelines apply to the entire repository.

## Coding Style
- Follow existing formatting for C++ and shell scripts.
- Document public functions using Doxygen style comments.
- write tests for every function
- use descriptive variable names
- comment extensively explaining everything 
- use comments to explain each step

## Commit Messages
- Use clear, concise messages in the present tense.

## Testing
Run the unit tests with CMake/CTest:
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure -VV 
```
The `-E FuseTestEnv` filter excludes the FUSE integration setup which requires root privileges.

## Changelog and Versioning
- Document all user-facing changes in `CHANGELOG.md` directly under a version heading.
- Use [Keep a Changelog](https://keepachangelog.com/) format and update the release date with each version.
- Bump the version number in the `VERSION` file using [Semantic Versioning](https://semver.org/); do not maintain an `[Unreleased]` section.
