---
description: Ingest a Pi session JSONL and review extracted memory quality
---
Ingest session file: $1

Steps:
1) Run `pi-memory ingest-session "$1" --dry-run` and report extracted counts.
2) If counts look right, run actual ingest.
3) Query latest decisions/findings/lessons/entities for this project.
4) Flag any low-signal or duplicate-looking items.
5) Propose cleanup actions.
