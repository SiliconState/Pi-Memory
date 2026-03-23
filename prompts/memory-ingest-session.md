---
description: Ingest a Pi session JSONL and review extracted memory quality
---
Ingest session file: `$1`
Project override (optional): `${2:-auto}`

Steps:
1) Run `pi-memory ingest-session "$1" --dry-run` (add `--project "$2"` when override is provided) and report extracted counts.
2) If counts look reasonable, run the real ingest command with the same arguments.
3) Query latest decisions/findings/lessons/entities for the target project and summarize quality.
4) Flag low-signal, duplicate-looking, or malformed extracted entries.
5) Propose concrete cleanup actions (exact commands where possible).

Output format:
- **Dry-run summary**
- **Ingest result**
- **Quality review**
- **Cleanup plan**
