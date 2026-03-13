#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync, readdirSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const quiet = process.argv.includes("--quiet");

const nativeDir = path.join(root, "native");
const source = path.join(nativeDir, "pi-memory.c");
const sqliteSource = path.join(nativeDir, "sqlite3.c");
const getoptCompat = path.join(nativeDir, "getopt_compat.c");
const outDir = path.join(os.homedir(), ".pi", "memory");
const defaultBinName = os.platform() === "win32" ? "pi-memory.exe" : "pi-memory";
const outBin = process.env.PI_MEMORY_BIN?.trim() || path.join(outDir, defaultBinName);

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
    path.join("C:", "Program Files", "LLVM", "bin", binaryName),
    path.join("C:", "Program Files", "LLVM", "bin", binaryName.replace(/\.exe$/i, "-22.exe")),
  ]);
}

function createCandidates() {
  const env = process.env.CC?.trim();
  const platform = os.platform();

  if (platform === "win32") {
    const envCandidate = env
      ? { command: env, args: env.toLowerCase().endsWith("cl") || env.toLowerCase() === "cl" ? msvcArgs() : gccStyleArgs(env) }
      : null;

    const discoveredClang = discoverWindowsCompilers("clang.exe").map((command) => ({ command, args: gccStyleArgs(command) }));
    const discoveredGcc = discoverWindowsCompilers("gcc.exe").map((command) => ({ command, args: gccStyleArgs(command) }));

    return [
      envCandidate,
      ...discoveredClang,
      ...discoveredGcc,
      { command: "clang", args: gccStyleArgs("clang") },
      { command: "gcc", args: gccStyleArgs("gcc") },
      { command: "cc", args: gccStyleArgs("cc") },
      { command: "cl", args: msvcArgs() },
    ].filter(Boolean);
  }

  return [
    { command: env || "cc", args: unixArgs() },
    ...(env ? [] : [{ command: "clang", args: unixArgs() }, { command: "gcc", args: unixArgs() }]),
  ];
}

function unixArgs() {
  const platform = os.platform();
  if (platform === "darwin") {
    return ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, getoptCompat, "-lm"];
  }
  return ["-Wall", "-Wextra", "-Wpedantic", "-O2", "-std=c11", "-o", outBin, source, sqliteSource, getoptCompat, "-lpthread", "-ldl", "-lm"];
}

function gccStyleArgs(_command) {
  return [
    "-D_CRT_SECURE_NO_WARNINGS",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-O2",
    "-std=c11",
    "-o",
    outBin,
    source,
    sqliteSource,
    getoptCompat,
  ];
}

function msvcArgs() {
  return [
    "/nologo",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/O2",
    "/W4",
    "/EHsc",
    `/Fe:${outBin}`,
    source,
    sqliteSource,
    getoptCompat,
  ];
}

mkdirSync(path.dirname(outBin), { recursive: true });

const candidates = createCandidates();
const attempts = [];
let result = null;
let selected = null;

for (const candidate of candidates) {
  log(`Compiling pi-memory with ${candidate.command} -> ${outBin}`);
  result = run(candidate.command, candidate.args);
  attempts.push({ candidate, result });
  if (result.status === 0) {
    selected = candidate;
    break;
  }
}

if (!selected || !result || result.status !== 0) {
  const details = attempts
    .map(({ candidate, result: attemptResult }) => {
      const out = [attemptResult.stdout, attemptResult.stderr].filter(Boolean).join("\n").trim() || "(no compiler output)";
      return `${candidate.command} ${candidate.args.map(quoteIfNeeded).join(" ")}\n${out}`;
    })
    .join("\n\n");

  console.error("\nFailed to compile pi-memory.");
  console.error("Tried compiler commands:\n");
  console.error(details);
  if (!quiet) {
    if (os.platform() === "win32") {
      console.error("\nInstall a basic Windows C compiler (clang, gcc, or cl) and retry:");
    } else {
      console.error("\nInstall a C compiler and retry:");
    }
    console.error("  npm run setup");
  }
  process.exit(result?.status ?? 1);
}

if (!existsSync(outBin)) {
  console.error(`Compilation reported success, but binary missing: ${outBin}`);
  process.exit(1);
}

if (os.platform() !== "win32") chmodSync(outBin, 0o755);
log(`Installed: ${outBin}`);
log("Done.");
