# AGENT Instructions

## Scope
These guidelines apply to the entire repository.

## Coding Style
- Follow existing formatting for C++ and shell scripts.
- Document public functions using Doxygen style comments.

## Commit Messages
- Use clear, concise messages in the present tense.

## Testing
Run the unit tests with CMake/CTest:
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure -VV -E FuseTestEnv
```
The `-E FuseTestEnv` filter excludes the FUSE integration setup which requires root privileges.

## Changelog and Versioning
- Document all user-facing changes in `CHANGELOG.md` under the `[Unreleased]` section.
- When releasing, move the notes into a new version heading and update the date following [Keep a Changelog](https://keepachangelog.com/) format.
- Bump the version number in the `VERSION` file to match the new release using [Semantic Versioning](https://semver.org/).
