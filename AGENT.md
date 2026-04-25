# Agent Behavior Specification

This document standardizes AI agent behavior. Read the corresponding section only when you need to perform the relevant action.

## Table of Contents

1. [Behavior](#0-behavior)
2. [Commit Specification](#1-commit-specification)
3. [Thinking](#2-thinking)
4. [Query Priority](#3-query-priority)

---

## 0. Behavior

- **Never commit without explicit permission**
- **Never modify library files or third-party dependencies**

---

## 1. Commit Specification

### Language

- **Allowed**: English

### Format

```
<type>(<scope>): <subject>

<body>

Model: <model-name> <model-provider>
Agent: <agent-name> <agent-email>
```

### Type

- `feat`: new feature
- `fix`: bug fix
- `refactor`: code refactoring (non-functional change)
- `docs`: documentation
- `test`: testing
- `chore`: build/tooling
- **Custom types**: permitted (e.g., `perf`, `ci`, `build`)

### Subject

- Use imperative mood ("add" not "added")
- No leading capital letter
- No trailing period
- Keep under 50 characters

### Body

- Each line ≤ 72 characters
- Explain WHY and HOW, not WHAT
- Use `-` bullet points for details

### Example

Proposed commit:

```
fix(usb): correct clock macros and port configuration

- Fix USB clock macros: USB1_OTG_HS -> USB_OTG_HS
- Change BOARD_TUD_RHPORT from 0 to 1 for correct port mapping
- Update ULPI, high-speed and full-speed pin initialization macros

Model: MiniMax-M2.7 MiniMax
Agent: opencode opencode@anomaly.co
```

Should I commit this change?

---

## 2. Thinking

The agent's thinking and output language should match the host machine's current locale. Check locale with `locale` or `echo $LANG` command.

---

## 3. Query Priority

Before any operation, query files in this order:

1. **AGENT.md** - this file
2. **README.md** - project overview and build instructions
3. **Project structure** - understand code organization
4. **Relevant headers** - understand interfaces and types
5. **Similar implementations** - reference existing code patterns