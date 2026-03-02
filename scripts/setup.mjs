#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const quiet = process.argv.includes("--quiet");

const source = path.join(root, "native", "pi-memory.c");
const outDir = path.join(os.homedir(), ".pi", "memory");
const outBin = process.env.PI_MEMORY_BIN?.trim() || path.join(outDir, "pi-memory");
const cc = process.env.CC || "cc";

function log(message) {
  if (!quiet) console.log(message);
}

function run(command, args) {
  return spawnSync(command, args, {
    stdio: quiet ? "pipe" : "inherit",
    encoding: "utf8",
  });
}

mkdirSync(path.dirname(outBin), { recursive: true });

const compileArgs = [
  "-Wall",
  "-Wextra",
  "-Wpedantic",
  "-O2",
  "-std=c11",
  "-o",
  outBin,
  source,
  "-lsqlite3",
];

log(`Compiling pi-memory -> ${outBin}`);
const result = run(cc, compileArgs);

if (result.status !== 0) {
  const details = [result.stdout, result.stderr].filter(Boolean).join("\n");
  console.error("\nFailed to compile pi-memory.");
  console.error("Compiler command:", [cc, ...compileArgs].join(" "));
  if (details) console.error(details);
  console.error("\nInstall build prerequisites (C compiler + sqlite3 dev headers) and retry:");
  console.error("  npm run setup");
  process.exit(result.status ?? 1);
}

if (!existsSync(outBin)) {
  console.error(`Compilation reported success, but binary missing: ${outBin}`);
  process.exit(1);
}

chmodSync(outBin, 0o755);
log(`Installed: ${outBin}`);
log("Done.");
