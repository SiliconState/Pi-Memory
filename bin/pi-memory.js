#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const setupScript = path.join(root, "scripts", "setup.mjs");

const binary = process.env.PI_MEMORY_BIN?.trim() || path.join(os.homedir(), ".pi", "memory", "pi-memory");

if (!existsSync(binary)) {
  const setup = spawnSync(process.execPath, [setupScript], { stdio: "inherit" });
  if (setup.status !== 0) process.exit(setup.status ?? 1);
}

const run = spawnSync(binary, process.argv.slice(2), { stdio: "inherit" });
process.exit(run.status ?? 1);
