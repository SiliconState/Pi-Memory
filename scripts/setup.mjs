#!/usr/bin/env node
/**
 * setup.mjs — install pi-memory binary
 *
 * 1. Try to copy a prebuilt binary for this platform
 * 2. If no prebuilt exists, compile from source (requires C compiler)
 */
import { spawnSync } from "node:child_process";
import { chmodSync, copyFileSync, existsSync, mkdirSync, readdirSync } from "node:fs";
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
    shell: false,
  });
}

function quoteIfNeeded(value) {
  return /\s/.test(value) ? `"${value}"` : value;
}

function unique(values) {
  return [...new Set(values.filter(Boolean))];
}

function collectWinGetCompilers(binaryName, packagePrefix) {
  const packagesDir = path.join(os.homedir(), "AppData", "Local", "Microsoft", "WinGet", "Packages");
  if (!existsSync(packagesDir)) return [];

  const hits = [];
  for (const entry of readdirSync(packagesDir, { withFileTypes: true })) {
    if (!entry.isDirectory() || !entry.name.startsWith(packagePrefix)) continue;
    const packageRoot = path.join(packagesDir, entry.name);
    for (const child of readdirSync(packageRoot, { withFileTypes: true })) {
      if (!child.isDirectory()) continue;
      const candidate = path.join(packageRoot, child.name, "bin", binaryName);
      if (existsSync(candidate)) hits.push(candidate);
    }
  }
  return hits;
}

function discoverWindowsCompilers(binaryName) {
  return unique([
    ...collectWinGetCompilers(binaryName, "MartinStorsjo.LLVM-MinGW.UCRT"),
    ...collectWinGetCompilers(binaryName, "MartinStorsjo.LLVM-MinGW.MSVCRT"),
    path.join("C:\\", "Program Files", "LLVM", "bin", binaryName),
    path.join("C:\\", "Program Files", "LLVM", "bin", binaryName.replace(/\.exe$/i, "-22.exe")),
  ]);
}

function unixArgs() {
  const platform = os.platform();
  if (platform === "darwin") {
    return ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, "-lm"];
  }
  return ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, "-lpthread", "-ldl", "-lm"];
}

function gccStyleArgs() {
  return [
    "-D_CRT_SECURE_NO_WARNINGS",
    "-D_CRT_NONSTDC_NO_DEPRECATE",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-O2",
    "-std=c11",
    "-o",
    outBin,
    source,
    sqliteSource,
    "-lm",
  ];
}

function msvcArgs() {
  return [
    "/nologo",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/D_CRT_NONSTDC_NO_DEPRECATE",
    "/O2",
    "/W3",
    `/Fe:${outBin}`,
    source,
    sqliteSource,
  ];
}

function createCandidates() {
  const env = process.env.CC?.trim();

  if (isWin) {
    const envBase = env ? path.basename(env).toLowerCase() : "";
    const envCandidate = env
      ? {
          command: env,
          args: ["cl", "cl.exe", "clang-cl", "clang-cl.exe"].includes(envBase) ? msvcArgs() : gccStyleArgs(),
        }
      : null;

    const discoveredClang = discoverWindowsCompilers("clang.exe").map((command) => ({ command, args: gccStyleArgs() }));
    const discoveredGcc = discoverWindowsCompilers("gcc.exe").map((command) => ({ command, args: gccStyleArgs() }));

    return [
      envCandidate,
      ...discoveredClang,
      ...discoveredGcc,
      { command: "clang", args: gccStyleArgs() },
      { command: "gcc", args: gccStyleArgs() },
      { command: "cc", args: gccStyleArgs() },
      { command: "clang-cl", args: msvcArgs() },
      { command: "cl", args: msvcArgs() },
    ].filter(Boolean);
  }

  return [
    { command: env || "cc", args: unixArgs() },
    ...(env ? [] : [{ command: "clang", args: unixArgs() }, { command: "gcc", args: unixArgs() }]),
  ];
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

const candidates = createCandidates();
const attempts = [];
let selected = null;
let result = null;

for (const candidate of candidates) {
  log(`Compiling pi-memory with ${candidate.command} -> ${outBin}`);
  result = run(candidate.command, candidate.args);
  attempts.push({ candidate, result });
  if (result.status === 0 && existsSync(outBin)) {
    selected = candidate;
    break;
  }
}

if (!selected || !result || result.status !== 0 || !existsSync(outBin)) {
  const details = attempts.length === 0
    ? "(no compiler candidates were available)"
    : attempts
        .map(({ candidate, result: attemptResult }) => {
          const output = [attemptResult.stdout, attemptResult.stderr].filter(Boolean).join("\n").trim() || "(no compiler output)";
          return `${candidate.command} ${candidate.args.map(quoteIfNeeded).join(" ")}\n${output}`;
        })
        .join("\n\n");

  console.error("\nFailed to compile pi-memory.");
  console.error("Tried compiler commands:\n");
  console.error(details);
  if (!quiet) {
    if (isWin) {
      console.error("\nInstall a Windows C compiler (clang, gcc, or cl) and retry:");
    } else {
      console.error("\nInstall a C compiler and retry:");
    }
    console.error("  npm run setup   (or: bun run setup)");
  }
  process.exit(result?.status ?? 1);
}

if (!isWin) chmodSync(outBin, 0o755);
log(`Installed: ${outBin}`);
log("Done.");
