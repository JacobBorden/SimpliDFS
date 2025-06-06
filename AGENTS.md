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
