#!/usr/bin/env node
/**
 * setup.mjs — install pi-memory binary
 *
 * 1. Try to copy a prebuilt binary for this platform
 * 2. If no prebuilt exists, compile from source (requires C compiler)
 */
import { spawnSync } from "node:child_process";
import { chmodSync, copyFileSync, existsSync, mkdirSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const quiet = process.argv.includes("--quiet");

const isWin = os.platform() === "win32";
const ext = isWin ? ".exe" : "";
const nativeDir = path.join(root, "native");
const source = path.join(nativeDir, "pi-memory.c");
const sqliteSource = path.join(nativeDir, "sqlite3.c");

const outDir = process.env.PI_MEMORY_BIN?.trim()
  ? path.dirname(process.env.PI_MEMORY_BIN)
  : path.join(os.homedir(), ".pi", "memory");
const outBin = process.env.PI_MEMORY_BIN?.trim() ||
  path.join(outDir, `pi-memory${ext}`);

function log(message) {
  if (!quiet) console.log(message);
}

function run(command, args) {
  return spawnSync(command, args, {
    stdio: quiet ? "pipe" : "inherit",
    encoding: "utf8",
  });
}

mkdirSync(outDir, { recursive: true });

/* ── Step 1: Try prebuilt binary ── */
const platformKey = `${os.platform()}-${os.arch()}`;
const prebuiltPath = path.join(root, "prebuilds", platformKey, `pi-memory${ext}`);

if (existsSync(prebuiltPath)) {
  log(`Copying prebuilt binary for ${platformKey} -> ${outBin}`);
  copyFileSync(prebuiltPath, outBin);
  if (!isWin) chmodSync(outBin, 0o755);
  log(`Installed: ${outBin}`);
  log("Done.");
  process.exit(0);
}

/* ── Step 2: Compile from source ── */
log(`No prebuilt for ${platformKey} — compiling from source...`);

const platform = os.platform();

if (isWin) {
  /* Try MSVC (cl.exe) first, then MinGW (gcc) */
  const msvcResult = run("cl", [
    "/O2", "/W3",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/D_CRT_NONSTDC_NO_DEPRECATE",
    `/Fe:${outBin}`,
    source, sqliteSource,
  ]);

  if (msvcResult.status === 0 && existsSync(outBin)) {
    log(`Installed: ${outBin}`);
    log("Done.");
    process.exit(0);
  }

  /* Try MinGW gcc */
  const gccResult = run("gcc", [
    "-Wall", "-Wextra", "-O2", "-std=c11",
    "-o", outBin,
    source, sqliteSource,
    "-lm",
  ]);

  if (gccResult.status === 0 && existsSync(outBin)) {
    log(`Installed: ${outBin}`);
    log("Done.");
    process.exit(0);
  }

  console.error("\nFailed to compile pi-memory on Windows.");
  console.error("Install Visual Studio Build Tools (cl.exe) or MinGW (gcc), then retry:");
  console.error("  npm run setup");
  process.exit(1);
} else {
  /* Unix: cc with platform-appropriate flags */
  const cc = process.env.CC || "cc";
  const compileArgs = platform === "darwin"
    ? ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, "-lm"]
    : ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, "-lpthread", "-ldl", "-lm"];

  log(`Compiling pi-memory -> ${outBin}`);
  const result = run(cc, compileArgs);

  if (result.status !== 0) {
    const details = [result.stdout, result.stderr].filter(Boolean).join("\n");
    console.error("\nFailed to compile pi-memory.");
    console.error("Compiler command:", [cc, ...compileArgs].join(" "));
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
}
