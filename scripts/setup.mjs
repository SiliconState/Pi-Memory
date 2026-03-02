#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const quiet = process.argv.includes("--quiet");

const nativeDir = path.join(root, "native");
const source = path.join(nativeDir, "pi-memory.c");
const sqliteSource = path.join(nativeDir, "sqlite3.c");
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

// Bundled SQLite amalgamation — no system sqlite3-dev required.
// Only needs: C compiler + pthreads + dl + math (standard on Linux/macOS).
const compileArgs = [
  "-Wall",
  "-Wextra",
  "-Wpedantic",
  "-O2",
  "-std=c11",
  "-o",
  outBin,
  source,
  sqliteSource,
  "-lpthread",
  "-ldl",
  "-lm",
];

// macOS doesn't need -ldl or -lpthread (included in system libs),
// and passing them may warn. Use platform-appropriate flags.
const platform = os.platform();
const platformArgs = platform === "darwin"
  ? ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, "-lm"]
  : compileArgs;

log(`Compiling pi-memory -> ${outBin}`);
const result = run(cc, platformArgs);

if (result.status !== 0) {
  const details = [result.stdout, result.stderr].filter(Boolean).join("\n");
  console.error("\nFailed to compile pi-memory.");
  console.error("Compiler command:", [cc, ...platformArgs].join(" "));
  if (details) console.error(details);
  if (!quiet) {
    console.error("\nInstall a C compiler and retry:");
    console.error("  npm run setup");
  }
  process.exit(result.status ?? 1);
}

if (!existsSync(outBin)) {
  console.error(`Compilation reported success, but binary missing: ${outBin}`);
  process.exit(1);
}

chmodSync(outBin, 0o755);
log(`Installed: ${outBin}`);
log("Done.");
