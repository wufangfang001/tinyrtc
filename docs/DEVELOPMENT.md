# Development Rules - TinyRTC

This document records development rules and best practices for TinyRTC project.

---

## Table of Contents

- [Working with Git Submodules](#working-with-git-submodules)
- [Documentation Requirements](#documentation-requirements)
- [Development Workflow](#development-workflow)

---

## Working with Git Submodules

### Rule 1: Understand Dependencies First

**When the project depends on third-party subprojects:**

1. **Always clone and checkout the submodule first**
   ```bash
   git clone main project
   cd project
   git submodule update --init --remote
   ```

2. **Read the dependency's documentation** to understand its functionality and APIs before coding:
   - Start with `README.md` for overview
   - Check `AGENTS.md` (if exists) for complete API documentation
   - Do not rely on memory or assumptions about the dependency

3. **Verify interface existence** - do not assume APIs exist without checking:
   - Check the actual header files
   - Verify function signatures
   - Confirm functionality matches your understanding

**Example lesson learned:**
- Initial architecture documentation incorrectly listed `aosl_random` which doesn't exist in AOSL
- Root cause: didn't check the actual AOSL API documentation before writing
- Fix: Always check the submodule's own documentation first

### Rule 2: Specific Version Compatibility

When the project only works with a specific version/branch of a dependency:
1. Specify the branch in `.gitmodules`
2. Use the correct clone order:
   ```bash
   git clone <main-repo>
   cd <main-repo>
   git submodule update --init --remote
   ```
   The `--remote` ensures the branch specified in `.gitmodules` is checked out.

---

## Documentation Requirements

Every project should have:

1. **ARCHITECTURE.md** - High-level architecture overview, module descriptions, layer diagram
2. **docs/memory/ARCHITECTURE_NOTES.md** - Key architecture decisions and tradeoffs
3. **docs/memory/API_CHANGES.md** - API change history
4. **docs/memory/BUGS.md** - Known issues and workarounds
5. **docs/DEVELOPMENT.md** - Development rules and best practices (this file)
6. Each submodule should have its own:
   - Functionality description
   - Architecture design
   - Public API documentation

---

## Development Workflow

Follow this workflow strictly:

1. **Requirements Alignment** - Confirm requirements with user
2. **Architecture Design** - Design or update architecture, document decisions
3. **Task Planning** - Break down into manageable tasks
4. **Code Writing** - Implement the feature
5. **Demo Testing** - Test the implementation works
6. **Git Commit** - Only commit after testing and user confirmation
7. **PR Submission** - Submit pull request for review

**Important:**
- Never commit before passing tests and getting user confirmation
- Keep commits atomic and well-described
- Update documentation when changing code/architecture
- Switching to a new project: clear old context and reload project documentation

---

## Documentation Update Rule

Every commit that changes:
- Architecture → update `ARCHITECTURE.md` and `ARCHITECTURE_NOTES.md`
- Public API → update `API_CHANGES.md`
- Known issues → update `BUGS.md`

Documentation must stay in sync with code.
