# Contributing to NX-Cast

Thank you for your interest in contributing.

## Development Setup

Install:

- devkitPro
- devkitA64
- libnx

Clone the repository:

```
git clone https://github.com/ode1l/NX-Cast
```

Build:

make

## Coding Style

- Use ISO C17 (or the closest supported by devkitPro)
- Keep modules separated
- Avoid platform-specific code outside platform layer

## Pull Requests

Before submitting:

- Code compiles
- No warnings
- Code formatted

PRs should describe:

- What changed
- Why it changed
- How it was tested

## Areas where help is needed

- Network protocol implementation
- Media pipeline
- Hardware decode
- Rendering
- Documentation
