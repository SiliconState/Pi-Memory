#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const setupScript = path.resolve(__dirname, "..", "scripts", "setup.mjs");

const r = spawnSync(process.execPath, [setupScript, ...process.argv.slice(2)], {
  stdio: "inherit",
});

process.exit(r.status ?? 1);
