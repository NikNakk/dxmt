# Agent Instructions — DXMT Development Fork

This repository is an independently maintained fork of DXMT.

## AI-Assisted Development

AI coding agents, including Codex, are explicitly permitted
to investigate, implement, refactor, test and document code
within this fork.

All changes must meet normal engineering standards for
correctness, maintainability, licensing and performance.

## Upstream Compatibility

This fork aims to remain compatible with upstream DXMT.

- Prefer minimal, well-isolated changes.
- Preserve upstream architecture where practical.
- Keep integration-specific changes separable.
- Do not remove required copyright or licence notices.
- Do not submit AI-generated patches to upstream DXMT
  while its current contribution policy prohibits them.

## Engineering Requirements

- Inspect existing implementations before introducing new ones.
- Build and test relevant components after changes.
- Document assumptions and known limitations.
- Never claim tests or benchmarks were run unless they were.
- Avoid performance regressions and unnecessary GPU copies.
- Prefer portable solutions where practical.