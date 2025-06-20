# Contributing to **SimpliDFS**

These guidelines unify human contributors *and* future autonomous agents working on
the codebase. Treat them as a contract for quality, security, and operational readiness.

---

## 1  Repository Scope

| Sub-system | Languages | Applies? |
|------------|-----------|----------|
| Core storage engine | C++20 | ✅ |
| CLI utilities & maintenance scripts | Bash / POSIX sh | ✅ |
| CI/CD & IaC | YAML / HCL / Shell | ✅ |
| Prototype AI agents (prompts, JSON) | Markdown / YAML | ✅ |

---

## 2  Coding Standards

### 2.1 C++

1. **Style** — follow the *LLVM C++ Style Guide*; enforced via  
   `clang-format` (`.clang-format` is committed).  
2. **Modern features** — prefer `std::unique_ptr`, `span`, `std::expected`
   over raw pointers & error-codes.  
3. **Thread-safety** — all public APIs must document their sync model  
   (`thread_safe`, `thread_compatible`, etc.).  
4. **Bounds & ownership** — enable AddressSanitizer/UBSan in CI builds.

### 2.2 Shell

* Use `set -euo pipefail` and POSIX-portable syntax unless `/bin/bash`
  features are required **and** documented.

### 2.3 Comments & Docs

* Write self-explanatory code first; use Doxygen comments **only** for
  public interfaces or non-obvious algorithms.
* Avoid “narrating” each line. Instead, provide invariants, rationale,
  and security considerations.

---

## 3  Testing & Quality Gates

| Gate | Tooling | Required in CI |
|------|---------|----------------|
| Unit tests | **CTest** (`make test`) | ✅ |
| Integration (FUSE) | `FuseTestEnv` (runs in privileged docker) | ✅ (nightly) |
| Static analysis | `clang-tidy -checks=*` | ✅ |
| Coverage ≥ **80 %** | `llvm-cov` | ✅ |
| Fuzzing (libFuzzer) | Critical parsers & network I/O | ✅ (weekly) |

---

## 4  Commit & PR Workflow

1. **Branch naming** `feat/<scope>`, `fix/<issue#>`, `chore/<misc>`.
2. **Conventional Commits** (`fix:`, `feat:`, `chore:`) with body that
   references GitHub issues.
3. Open a **draft PR early**; CI must pass before requesting review.
4. PR needs **one approval** and must be **squash-merged** to `main`.

---

## 5  Changelog & Versioning

* **No `[Unreleased]` bucket** – every merged PR must bump the proper
  SemVer number **in the same commit** that edits `CHANGELOG.md`.
  - **Patch** bump (`1.2.3 → 1.2.4`) for bug fixes or doc tweaks.  
  - **Minor** bump (`1.2.3 → 1.3.0`) for backward-compatible features.  
  - **Major** bump (`1.2.3 → 2.0.0`) when storage-format or API breaks.
* The `CHANGELOG.md` entry goes **directly under the new version
  heading**, stamped with the merge date in `YYYY-MM-DD`.
* `VERSION` file **must match** the heading exactly; CI will block if it
  doesn’t.
* A Git tag (`vX.Y.Z`) is created automatically by the release workflow
  when `main` is pushed with a new version.


---

## 6  Security Rules

* Never commit secrets; CI checks for key patterns via `gitleaks`.
* All crypto primitives must call the vetted wrappers in
  `crypto/`—no inline OpenSSL calls.
* Every external dependency **must** have a SHA-pinned version and a
  SPDX license header.

---

## 7  Checklist for Merging

- [ ] `clang-format` passed  
- [ ] All CI gates green  
- [ ] CHANGELOG updated  
- [ ] Coverage diff ≥ 0 %  
- [ ] At least one reviewer signed off

---

## 8  Meta-Agent Guidance

If you are an autonomous coding agent:

* Print the checklist above before exiting.
* Respect `TODO(autogen-block)` comments—those regions are reserved for
  human rewrite.
