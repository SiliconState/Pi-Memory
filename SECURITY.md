# Security Policy

## Reporting a vulnerability

Please report security issues privately via GitHub Security Advisories or direct maintainer contact.

Include:
- affected version
- reproduction steps
- impact assessment
- suggested mitigation (if any)

## Scope highlights

- Native C binary (`native/pi-memory.c`)
- SQLite interactions
- extension command execution boundaries
- session ingest parsing behavior

## Hardening baseline

- prepared SQL statements
- bounded buffers
- best-effort non-fatal extension hooks
- local-only default storage (`~/.pi/memory/memory.db`)
