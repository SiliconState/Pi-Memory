/**
 * Auto-sync pi-memory before compaction, trigger early compaction,
 * use cross-provider model fallback for summarization, and bridge
 * final rate-limit failures to a backup model/provider.
 *
 * v2: Also stores compaction summaries as findings, passes session-id
 * to all pi-memory calls, and updates project state on session shutdown.
 *
 * This avoids hard failures when the active model/provider is out of quota
 * or rate-limited by trying configured backup providers before giving up.
 * Provider failover is model-agnostic — it works with whatever models
 * the user has configured and keyed, no hardcoded provider preferences.
 */

import { complete, type Model } from "@mariozechner/pi-ai";
import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { convertToLlm, serializeConversation } from "@mariozechner/pi-coding-agent";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

const DEFAULT_COMPACT_THRESHOLD = parseThresholdValue(process.env.PI_COMPACT_THRESHOLD, 0.6); // 60% default
const THRESHOLD_ENTRY_TYPE = "memory-compact-threshold";
const PROVIDER_COOLDOWN_MS = 30 * 60 * 1000; // 30 minutes
const COMPACTION_MODEL_TIMEOUT_MS = parsePositiveInt(process.env.PI_COMPACTION_MODEL_TIMEOUT_MS, 15000);
const COMPACTION_WATCHDOG_MS = parsePositiveInt(process.env.PI_COMPACTION_WATCHDOG_MS, 60000);
const MAX_MODEL_ATTEMPTS = parsePositiveInt(process.env.PI_COMPACTION_MAX_MODEL_ATTEMPTS, 5);

// Preferred compaction models (ordered). Only used if available + keyed.
// Override with PI_COMPACTION_FALLBACK_MODELS env var (comma-separated).
const DEFAULT_FALLBACK_MODELS: string[] = [];

interface ResumableState {
  lastIntent: string | null;
}

interface CompactionPreparationLike {
  messagesToSummarize?: unknown[];
  turnPrefixMessages?: unknown[];
  previousSummary?: string;
  firstKeptEntryId: string;
  tokensBefore: number;
  fileOps?: unknown;
}

interface ThresholdEntryData {
  threshold?: number;
  updatedAt?: number;
  source?: string;
}

interface AutoRetryEndEventLike {
  success: boolean;
  attempt: number;
  finalError?: string;
}

function parsePositiveInt(raw: string | undefined, fallback: number): number {
  const parsed = Number.parseInt(raw ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function parseThresholdValue(raw: string | undefined, fallback: number): number {
  if (!raw) return fallback;

  const trimmed = raw.trim().replace(/%$/, "");
  const value = Number.parseFloat(trimmed);
  if (!Number.isFinite(value)) return fallback;

  const ratio = value > 1 ? value / 100 : value;
  if (ratio <= 0.01 || ratio >= 0.99) return fallback;

  return ratio;
}

function formatThresholdPercent(ratio: number): string {
  return `${Math.round(ratio * 100)}%`;
}

function getProjectKey(cwd: string): string {
  const pinned = process.env.PI_MEMORY_PROJECT?.trim();
  if (pinned) return pinned;

  try {
    const memoryFile = path.join(cwd, "MEMORY.md");
    if (existsSync(memoryFile)) {
      const header = readFileSync(memoryFile, "utf8").match(/^#\s+Memory\s+[—-]\s+(.+)$/m)?.[1]?.trim();
      if (header) return header;
    }
  } catch {
    // Non-fatal; fall back to cwd basename.
  }

  return path.basename(cwd);
}

interface ExecResultLike {
  code?: number;
  stdout?: string;
  stderr?: string;
}

function hasCommandUnavailableHint(text: string | undefined): boolean {
  if (!text) return false;
  const m = text.toLowerCase();
  return (
    m.includes("not found") ||
    m.includes("no such file") ||
    m.includes("enoent") ||
    m.includes("permission denied") ||
    m.includes("access is denied") ||
    m.includes("eacces")
  );
}

function isCommandUnavailableResult(result: ExecResultLike | undefined): boolean {
  if (!result) return false;
  if (result.code === 127 || result.code === 126) return true;

  return (
    hasCommandUnavailableHint(result.stderr) ||
    hasCommandUnavailableHint(result.stdout)
  );
}

function isCommandLaunchError(error: unknown): boolean {
  if (!(error instanceof Error)) return false;
  return hasCommandUnavailableHint(error.message);
}

function getPreferredPiMemoryBin(): string | undefined {
  const fromEnv = process.env.PI_MEMORY_BIN?.trim();
  if (fromEnv) return fromEnv;

  const home = process.env.HOME ?? process.env.USERPROFILE ?? "";
  if (!home) return undefined;

  return path.join(
    home,
    ".pi",
    "memory",
    process.platform === "win32" ? "pi-memory.exe" : "pi-memory"
  );
}

async function execPiMemory(
  pi: ExtensionAPI,
  args: string[],
  options?: { timeout?: number; signal?: AbortSignal }
) {
  const preferred = getPreferredPiMemoryBin();

  if (preferred && preferred !== "pi-memory") {
    try {
      const result = await pi.exec(preferred, args, options);
      if (!isCommandUnavailableResult(result as ExecResultLike)) return result;
    } catch (error) {
      if (!isCommandLaunchError(error)) throw error;
    }
  }

  return pi.exec("pi-memory", args, options);
}

function isQuotaError(message: string): boolean {
  const m = message.toLowerCase();
  return (
    m.includes("429") ||
    m.includes("rate limit") ||
    m.includes("rate_limit") ||
    m.includes("too many requests") ||
    m.includes("quota") ||
    m.includes("insufficient") ||
    m.includes("balance")
  );
}

function isTransientProviderError(message: string): boolean {
  const m = message.toLowerCase();
  return (
    isQuotaError(message) ||
    m.includes("server_error") ||
    m.includes("server error") ||
    m.includes("internal error") ||
    m.includes("internal server error") ||
    m.includes("500") ||
    m.includes("502") ||
    m.includes("503") ||
    m.includes("504") ||
    m.includes("overloaded") ||
    m.includes("service unavailable") ||
    m.includes("unavailable") ||
    m.includes("bad gateway") ||
    m.includes("gateway timeout") ||
    m.includes("connection error") ||
    m.includes("connection refused") ||
    m.includes("other side closed") ||
    m.includes("fetch failed") ||
    m.includes("upstream connect") ||
    m.includes("reset before headers") ||
    m.includes("terminated") ||
    m.includes("econnreset") ||
    m.includes("etimedout") ||
    m.includes("timeout")
  );
}

function splitModelKey(modelKey: string): { provider: string; modelId: string } | null {
  const [provider, ...rest] = modelKey.split("/");
  const modelId = rest.join("/");
  if (!provider || !modelId) return null;
  return { provider, modelId };
}

function parseFallbackModelEnv(): string[] {
  const raw = process.env.PI_COMPACTION_FALLBACK_MODELS?.trim();
  if (!raw) return DEFAULT_FALLBACK_MODELS;

  return raw
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
}

function withTimeoutSignal(parentSignal: AbortSignal | undefined, timeoutMs: number) {
  const controller = new AbortController();
  let timedOut = false;

  const timeout = setTimeout(() => {
    timedOut = true;
    controller.abort();
  }, timeoutMs);
  timeout.unref?.();

  const onParentAbort = () => controller.abort();

  if (parentSignal?.aborted) {
    onParentAbort();
  } else {
    parentSignal?.addEventListener("abort", onParentAbort, { once: true });
  }

  return {
    signal: controller.signal,
    timedOut: () => timedOut,
    cleanup: () => {
      clearTimeout(timeout);
      parentSignal?.removeEventListener("abort", onParentAbort);
    },
  };
}

function buildCompactPrompt(conversationText: string, previousSummary?: string, customInstructions?: string): string {
  const previous = previousSummary ? `\n\nPrevious compaction summary (retain important unresolved items):\n${previousSummary}` : "";
  const custom = customInstructions ? `\n\nUser compaction instructions:\n${customInstructions}` : "";

  return [
    "You are compacting a coding session so work can continue seamlessly.",
    "Produce structured markdown with sections:",
    "- Goal",
    "- Constraints & Preferences",
    "- Progress (Done / In Progress / Blocked)",
    "- Key Decisions",
    "- Next Steps",
    "- Critical Context",
    "",
    "Be concise but complete. Preserve exact technical details that matter (APIs, file paths, formulas, addresses, errors).",
    previous,
    custom,
    "",
    "<conversation>",
    conversationText,
    "</conversation>",
  ].join("\n");
}

function buildHeuristicFallbackSummary(prep: CompactionPreparationLike, conversationText: string): string {
  const excerpt = conversationText.split("\n").slice(-40).join("\n");

  return [
    "## Goal",
    "Continue the current task without losing context.",
    "",
    "## Constraints & Preferences",
    "- Model-based compaction summary was unavailable (provider quota/key issue).",
    "- Preserve actionable next steps and technical state.",
    "",
    "## Progress",
    "### Done",
    "- Session context compacted with fallback summary path.",
    "",
    "### In Progress",
    "- Continue from most recent intent and pending work.",
    "",
    "### Blocked",
    "- One or more compaction providers returned quota/auth failures.",
    "",
    "## Key Decisions",
    "- Enabled cross-provider fallback for compaction summarization.",
    "",
    "## Next Steps",
    "1. Continue from latest user intent in this branch.",
    "2. Resolve provider billing/key issues if full LLM summaries are desired.",
    "",
    "## Critical Context",
    prep.previousSummary ? `### Previous Summary\n${prep.previousSummary}` : "### Previous Summary\n- None",
    "",
    "### Recent Conversation Excerpt",
    "```",
    excerpt || "(no recent excerpt available)",
    "```",
  ].join("\n");
}

export default function (pi: ExtensionAPI) {
  let compactionTriggered = false;
  let autoCompactionPending = false;
  let compactionWatchdog: ReturnType<typeof setTimeout> | null = null;
  let compactThreshold = DEFAULT_COMPACT_THRESHOLD;

  const providerBackoffUntil = new Map<string, number>();
  let resumableState: ResumableState = {
    lastIntent: null,
  };

  // v2: capture last compaction summary for post-compact storage
  let lastCompactionSummary: string | null = null;
  let autoFailoverBridgeActive = false;

  const piRuntime = pi as ExtensionAPI & {
    on(event: "auto_retry_end", handler: (event: AutoRetryEndEventLike, ctx: any) => Promise<void> | void): void;
  };

  // v2: helper to get session ID from context
  const getSessionId = (ctx: { sessionManager: { getSessionId(): string } }) => {
    try {
      return ctx.sessionManager.getSessionId();
    } catch {
      return undefined;
    }
  };

  // v2: helper to build session-id args
  const sessionIdArgs = (ctx: { sessionManager: { getSessionId(): string } }): string[] => {
    const sid = getSessionId(ctx);
    return sid ? ["--session-id", sid] : [];
  };

  /**
   * Get eligible failover models — model-agnostic.
   * Picks any available model from a DIFFERENT provider than the current one,
   * skipping providers on cooldown. No hardcoded provider preferences.
   */
  const getEligibleFailoverModels = async (ctx: {
    modelRegistry: {
      find(provider: string, modelId: string): Model<any> | undefined;
      getApiKey(model: Model<any>): Promise<string | undefined>;
      getAvailable(): Model<any>[];
    };
    model: Model<any>;
  }) => {
    const now = Date.now();
    const currentKey = `${ctx.model.provider}/${ctx.model.id}`;
    const seen = new Set<string>();
    const eligible: Model<any>[] = [];

    for (const model of ctx.modelRegistry.getAvailable()) {
      const key = `${model.provider}/${model.id}`;
      if (key === currentKey) continue;
      if (seen.has(key)) continue;

      // Skip models on the same provider (since that provider just failed)
      if (model.provider === ctx.model.provider) continue;

      const backoffUntil = providerBackoffUntil.get(model.provider) ?? 0;
      if (backoffUntil > now) continue;

      const apiKey = await ctx.modelRegistry.getApiKey(model);
      if (!apiKey) continue;

      seen.add(key);
      eligible.push(model);
    }

    return eligible;
  };

  const shouldBridgeRetryFailure = (errorMessage?: string) => {
    if (!errorMessage) return false;
    return isTransientProviderError(errorMessage);
  };

  const needsCompactionForModel = (ctx: { getContextUsage(): { tokens: number; contextWindow: number } | undefined }, model: Model<any>) => {
    const usage = ctx.getContextUsage();
    if (!usage || usage.tokens == null) return false;

    const contextWindow = model.contextWindow ?? 0;
    if (contextWindow <= 0) return false;

    return usage.tokens / contextWindow >= compactThreshold;
  };

  const buildFailoverResumeMessage = (fromModel: Model<any>, toModel: Model<any>, failure?: string) => {
    const failureNote = failure
      ? `Failure on previous model: ${failure}.`
      : `Previous model ${fromModel.provider}/${fromModel.id} failed after automatic retries.`;

    if (resumableState.lastIntent) {
      return [
        failureNote,
        `Continue from the current session state using ${toModel.provider}/${toModel.id}.`,
        `Do not restart from scratch unless needed.`,
        `Last user intent: ${resumableState.lastIntent}`,
      ].join(" ");
    }

    return [
      failureNote,
      `Continue from the current session state using ${toModel.provider}/${toModel.id}.`,
      "Do not restart from scratch unless needed.",
    ].join(" ");
  };

  const clearCompactionWatchdog = () => {
    if (compactionWatchdog) {
      clearTimeout(compactionWatchdog);
      compactionWatchdog = null;
    }
  };

  const releaseCompactionLock = () => {
    clearCompactionWatchdog();
    compactionTriggered = false;
    autoCompactionPending = false;
  };

  const persistThreshold = (threshold: number, source: string) => {
    try {
      const payload: ThresholdEntryData = {
        threshold,
        updatedAt: Date.now(),
        source,
      };
      pi.appendEntry(THRESHOLD_ENTRY_TYPE, payload);
    } catch {
      // Non-fatal
    }
  };

  pi.on("session_start", async (_event, ctx) => {
    compactThreshold = DEFAULT_COMPACT_THRESHOLD;

    for (const entry of ctx.sessionManager.getBranch()) {
      const anyEntry = entry as {
        type?: string;
        customType?: string;
        data?: ThresholdEntryData;
      };

      if (anyEntry.type !== "custom" || anyEntry.customType !== THRESHOLD_ENTRY_TYPE) {
        continue;
      }

      const restored = parseThresholdValue(
        anyEntry.data?.threshold != null ? String(anyEntry.data.threshold) : undefined,
        -1
      );
      if (restored > 0) {
        compactThreshold = restored;
      }
    }

    ctx.ui.setStatus("compact-threshold", `auto-compact: ${formatThresholdPercent(compactThreshold)}`);
  });

  pi.registerCommand("compact-threshold", {
    description: "Show or set auto-compaction threshold (examples: 75, 0.75, 75%, reset)",
    handler: async (args, ctx) => {
      const raw = (args ?? "").trim();

      if (!raw) {
        ctx.ui.notify(
          `Auto-compaction threshold is ${formatThresholdPercent(compactThreshold)} (default ${formatThresholdPercent(DEFAULT_COMPACT_THRESHOLD)}).`,
          "info"
        );
        return;
      }

      if (raw === "reset" || raw === "default") {
        compactThreshold = DEFAULT_COMPACT_THRESHOLD;
        persistThreshold(compactThreshold, "reset");
        ctx.ui.setStatus("compact-threshold", `auto-compact: ${formatThresholdPercent(compactThreshold)}`);
        ctx.ui.notify(`Auto-compaction threshold reset to ${formatThresholdPercent(compactThreshold)}.`, "info");
        return;
      }

      const parsed = parseThresholdValue(raw, -1);
      if (parsed <= 0) {
        ctx.ui.notify("Invalid threshold. Use a value like 75, 0.75, or 75%.", "error");
        return;
      }

      compactThreshold = parsed;
      persistThreshold(compactThreshold, "command");
      ctx.ui.setStatus("compact-threshold", `auto-compact: ${formatThresholdPercent(compactThreshold)}`);
      ctx.ui.notify(`Auto-compaction threshold set to ${formatThresholdPercent(compactThreshold)}.`, "info");
    },
  });

  // ── Track user intent for resumption ───────────────────────────────
  pi.on("input", async (event) => {
    if (event.source === "extension") return { action: "continue" };

    const text = event.text.trim();
    if (text.startsWith("/") || text.length === 0) {
      return { action: "continue" };
    }

    resumableState.lastIntent = text;
    return { action: "continue" };
  });

  // ── Final retry failure bridge: fail over to another provider ──────
  piRuntime.on("auto_retry_end", async (event, ctx) => {
    if (event.success) return;
    if (autoFailoverBridgeActive) return;
    if (!shouldBridgeRetryFailure(event.finalError)) return;
    if (!ctx.model) return;

    const candidates = await getEligibleFailoverModels(ctx);

    // No other providers available — preserve default Pi behavior.
    if (candidates.length === 0) return;

    const currentModel = ctx.model;
    providerBackoffUntil.set(currentModel.provider, Date.now() + PROVIDER_COOLDOWN_MS);
    autoFailoverBridgeActive = true;

    try {
      for (const candidate of candidates) {
        const switched = await pi.setModel(candidate);
        if (!switched) continue;

        const compactFirst = needsCompactionForModel(ctx, candidate);
        if (compactFirst) {
          ctx.ui.notify(
            `Provider failure after ${event.attempt} retries on ${currentModel.provider}/${currentModel.id}. ` +
            `Switched to ${candidate.provider}/${candidate.id}; compaction will run before auto-resume if needed.`,
            "warning"
          );
          return;
        }

        ctx.ui.notify(
          `Provider failure after ${event.attempt} retries on ${currentModel.provider}/${currentModel.id}. ` +
          `Switched to ${candidate.provider}/${candidate.id} and resuming automatically.`,
          "warning"
        );

        try {
          pi.sendUserMessage(buildFailoverResumeMessage(currentModel, candidate, event.finalError), { deliverAs: "followUp" });
        } catch (error) {
          const message = error instanceof Error ? error.message : String(error);
          ctx.ui.notify(`Auto-resume after model failover could not be queued: ${message}`, "warning");
        }
        return;
      }

      ctx.ui.notify(
        "Failover bridge: no configured backup model could be activated. Default failure preserved.",
        "warning"
      );
    } finally {
      autoFailoverBridgeActive = false;
    }
  });

  // ── Before compact: sync memory + custom cross-provider summary ────
  pi.on("session_before_compact", async (event, ctx) => {
    const project = getProjectKey(ctx.cwd);

    // v2: reset captured summary for fresh compaction
    lastCompactionSummary = null;

    // Best-effort memory sync (never block compaction)
    try {
      let result = await execPiMemory(pi, ["sync", "MEMORY.md", "--limit", "15"], { timeout: 10000 });
      if (result.code !== 0) {
        result = await execPiMemory(pi, ["sync", "MEMORY.md", "--project", project, "--limit", "15"], {
          timeout: 10000,
        });
      }
      if (result.code === 0) {
        ctx.ui.notify("pi-memory synced before compaction", "info");
      }
    } catch {
      // Non-fatal
    }

    const prep = event.preparation as unknown as CompactionPreparationLike;
    const messagesToSummarize = prep.messagesToSummarize ?? [];
    const turnPrefixMessages = prep.turnPrefixMessages ?? [];
    const allMessages = [...messagesToSummarize, ...turnPrefixMessages];

    // If nothing to summarize, let default behavior continue.
    if (allMessages.length === 0) {
      return undefined;
    }

    const conversationText = serializeConversation(convertToLlm(allMessages as never[]));
    const prompt = buildCompactPrompt(conversationText, prep.previousSummary, event.customInstructions);

    const now = Date.now();
    const preferred = parseFallbackModelEnv();
    const preferredModels = preferred
      .map((entry) => {
        const parsed = splitModelKey(entry);
        if (!parsed) return undefined;
        return ctx.modelRegistry.find(parsed.provider, parsed.modelId);
      })
      .filter((m): m is Model<any> => Boolean(m));

    const availableModels = ctx.modelRegistry.getAvailable();

    // Ordered candidates: current model -> preferred list -> all available
    const candidates: Model<any>[] = [];
    const seen = new Set<string>();

    const pushCandidate = (model: Model<any> | undefined) => {
      if (!model) return;
      const key = `${model.provider}/${model.id}`;
      if (seen.has(key)) return;
      const backoffUntil = providerBackoffUntil.get(model.provider) ?? 0;
      if (backoffUntil > now) return;
      seen.add(key);
      candidates.push(model);
    };

    pushCandidate(ctx.model);
    preferredModels.forEach(pushCandidate);
    availableModels.forEach(pushCandidate);

    const failures: string[] = [];
    const candidateModels = candidates.slice(0, MAX_MODEL_ATTEMPTS);

    for (const model of candidateModels) {
      if (event.signal.aborted) {
        return undefined;
      }

      let timeoutCtx: ReturnType<typeof withTimeoutSignal> | undefined;

      try {
        const apiKey = await ctx.modelRegistry.getApiKey(model);
        if (!apiKey) {
          failures.push(`${model.provider}/${model.id}: no API key`);
          continue;
        }

        timeoutCtx = withTimeoutSignal(event.signal, COMPACTION_MODEL_TIMEOUT_MS);

        const response = await complete(
          model,
          {
            messages: [
              {
                role: "user",
                content: [{ type: "text", text: prompt }],
                timestamp: Date.now(),
              },
            ],
          },
          {
            apiKey,
            maxTokens: 4096,
            signal: timeoutCtx.signal,
          }
        );

        const summary = response.content
          .filter((c): c is { type: "text"; text: string } => c.type === "text")
          .map((c) => c.text)
          .join("\n")
          .trim();

        if (!summary) {
          failures.push(`${model.provider}/${model.id}: empty summary`);
          continue;
        }

        ctx.ui.notify(`Compaction summary generated via ${model.provider}/${model.id}`, "info");

        // v2: capture for post-compact storage
        lastCompactionSummary = summary;

        return {
          compaction: {
            summary,
            firstKeptEntryId: prep.firstKeptEntryId,
            tokensBefore: prep.tokensBefore,
            details: prep.fileOps,
          },
        };
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        const reason = timeoutCtx?.timedOut()
          ? `timeout after ${COMPACTION_MODEL_TIMEOUT_MS}ms`
          : message;

        failures.push(`${model.provider}/${model.id}: ${reason}`);

        if (isTransientProviderError(message)) {
          providerBackoffUntil.set(model.provider, Date.now() + PROVIDER_COOLDOWN_MS);
        }

        if (event.signal.aborted) {
          return undefined;
        }
      } finally {
        timeoutCtx?.cleanup();
      }
    }

    // Deterministic fallback so compaction still succeeds even if ALL model attempts fail.
    const fallbackSummary = buildHeuristicFallbackSummary(prep, conversationText);
    const failureSummary = failures.slice(0, 2).join(" | ") || "no eligible model/API key was available";
    ctx.ui.notify(`Compaction model fallback used. ${failureSummary}`, "warning");

    // v2: capture for post-compact storage
    lastCompactionSummary = fallbackSummary;

    return {
      compaction: {
        summary: fallbackSummary,
        firstKeptEntryId: prep.firstKeptEntryId,
        tokensBefore: prep.tokensBefore,
        details: prep.fileOps,
      },
    };
  });

  // ── After compact: store summary + reset lock + auto-continue ──────
  pi.on("session_compact", async (_event, ctx) => {
    const wasAutoCompaction = autoCompactionPending;
    releaseCompactionLock();

    // v2: Store compaction summary as a finding in pi-memory
    if (lastCompactionSummary) {
      const project = getProjectKey(ctx.cwd);
      const sid = getSessionId(ctx);
      const source = sid ? `session:${sid}` : "compaction";
      try {
        const args = [
          "log", "finding", lastCompactionSummary,
          "--category", "compaction-summary",
          "--source", source,
          "--confidence", "verified",
          "--tags", "auto-compaction",
          "--project", project,
          ...sessionIdArgs(ctx),
        ];
        await execPiMemory(pi, args, { timeout: 5000 });
        ctx.ui.notify("Compaction summary stored in pi-memory", "info");
      } catch {
        // Non-fatal
      }
      lastCompactionSummary = null;
    }

    const shouldResume = wasAutoCompaction;
    if (!shouldResume) return;

    const resumeMessage = resumableState.lastIntent
      ? `Continue from where we left off after compaction. Last user intent: ${resumableState.lastIntent}`
      : "Continue from where we left off after compaction. Last task context was preserved via pi-memory sync.";

    ctx.ui.notify("Auto-resuming after compaction...", "info");

    try {
      pi.sendUserMessage(resumeMessage, { deliverAs: "followUp" });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      ctx.ui.notify(`Auto-resume enqueue failed: ${message}`, "warning");
    }
  });

  // ── Monitor context usage and trigger early compaction ─────────────
  pi.on("turn_end", async (_event, ctx) => {
    if (compactionTriggered) return;

    const usage = ctx.getContextUsage();
    if (!usage || usage.tokens == null) return;

    const contextWindow = usage.contextWindow ?? 0;
    if (contextWindow <= 0) return;

    const ratio = usage.tokens / contextWindow;
    if (ratio < compactThreshold) return;

    compactionTriggered = true;
    autoCompactionPending = true;

    clearCompactionWatchdog();
    compactionWatchdog = setTimeout(() => {
      releaseCompactionLock();
      ctx.ui.notify(
        `Compaction watchdog released lock after ${Math.round(COMPACTION_WATCHDOG_MS / 1000)}s.`,
        "warning"
      );
    }, COMPACTION_WATCHDOG_MS);
    compactionWatchdog.unref?.();

    const project = getProjectKey(ctx.cwd);
    try {
      await execPiMemory(pi, [
        "state",
        project,
        "--summary",
        `Auto-compacting at ${Math.round(ratio * 100)}% context usage`,
      ], { timeout: 5000 });
    } catch {
      // Non-fatal
    }

    ctx.ui.notify(
      `Context at ${Math.round(ratio * 100)}% (threshold ${formatThresholdPercent(compactThreshold)}) — triggering compaction`,
      "warning"
    );

    try {
      ctx.compact({
        customInstructions:
          "Preserve all architectural decisions, contract addresses, test counts, and concrete next steps.",
        onComplete: () => {
          ctx.ui.notify("Compaction complete", "info");
        },
        onError: (err) => {
          ctx.ui.notify(`Compaction failed: ${err.message}`, "error");
          releaseCompactionLock();
        },
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      ctx.ui.notify(`Compaction trigger failed: ${message}`, "error");
      releaseCompactionLock();
    }
  });

  pi.on("session_shutdown", async (_event, ctx) => {
    clearCompactionWatchdog();

    const project = getProjectKey(ctx.cwd);

    // v2.1: Auto-ingest current session file FIRST (updates rollups)
    try {
      const sessionFile = ctx.sessionManager.getSessionFile();
      if (sessionFile) {
        await execPiMemory(pi, [
          "ingest-session", sessionFile,
          "--project", project,
        ], { timeout: 20000 });
      }
    } catch {
      // Non-fatal — don't block shutdown
    }

    // v2.1: Update project state with session summary
    try {
      const usage = ctx.getContextUsage();
      const model = ctx.model;
      const sid = getSessionId(ctx);

      const parts: string[] = [];
      if (model) parts.push(`model=${model.provider}/${model.id}`);
      if (usage?.tokens) parts.push(`${Math.round(usage.tokens / 1000)}k tokens used`);
      if (sid) parts.push(`session=${sid.slice(0, 8)}`);
      parts.push(`ended ${new Date().toISOString().slice(0, 19)}Z`);

      const summary = `Session ended: ${parts.join(", ")}`;
      await execPiMemory(pi, [
        "state", project,
        "--summary", summary,
      ], { timeout: 5000 });
    } catch {
      // Non-fatal — don't block shutdown
    }

    // v2.1: Sync MEMORY.md on shutdown (best-effort)
    try {
      await execPiMemory(pi, [
        "sync", "MEMORY.md",
        "--project", project,
        "--limit", "15",
      ], { timeout: 8000 });
    } catch {
      // Non-fatal
    }
  });

  // ── Model switch: check if new model's smaller window puts us over threshold ─
  pi.on("model_select", async (event, ctx) => {
    if (compactionTriggered) return;

    const prev = event.previousModel;
    const next = event.model;
    if (!prev || !next) return;

    // Only care when switching to a SMALLER context window
    const prevWindow = prev.contextWindow ?? 0;
    const nextWindow = next.contextWindow ?? 0;
    if (nextWindow >= prevWindow || nextWindow <= 0) return;

    // Give the runtime a tick to update model reference before checking usage
    await new Promise((resolve) => setTimeout(resolve, 50));

    const usage = ctx.getContextUsage();
    if (!usage || usage.tokens == null) return;

    const contextWindow = usage.contextWindow ?? 0;
    if (contextWindow <= 0) return;

    const ratio = usage.tokens / contextWindow;
    if (ratio < compactThreshold) {
      if (ratio > compactThreshold - 0.10) {
        ctx.ui.notify(
          `Model switched to ${next.name ?? next.id} (${Math.round(nextWindow / 1000)}k window). ` +
          `Context at ${Math.round(ratio * 100)}% — approaching compaction threshold.`,
          "warning"
        );
      }
      return;
    }

    // Context is already over threshold for the new model!
    compactionTriggered = true;
    autoCompactionPending = true;

    clearCompactionWatchdog();
    compactionWatchdog = setTimeout(() => {
      releaseCompactionLock();
      ctx.ui.notify(
        `Compaction watchdog released lock after ${Math.round(COMPACTION_WATCHDOG_MS / 1000)}s.`,
        "warning"
      );
    }, COMPACTION_WATCHDOG_MS);
    compactionWatchdog.unref?.();

    ctx.ui.notify(
      `Model switched: ${prev.name ?? prev.id} (${Math.round(prevWindow / 1000)}k) → ` +
      `${next.name ?? next.id} (${Math.round(nextWindow / 1000)}k). ` +
      `Context at ${Math.round(ratio * 100)}% — triggering immediate compaction!`,
      "warning"
    );

    try {
      ctx.compact({
        customInstructions:
          "Preserve all architectural decisions, contract addresses, test counts, and concrete next steps. " +
          "Context window shrank due to model switch — aggressive compaction needed.",
        onComplete: () => {
          ctx.ui.notify("Post-model-switch compaction complete", "info");
        },
        onError: (err) => {
          ctx.ui.notify(`Post-model-switch compaction failed: ${err.message}`, "error");
          releaseCompactionLock();
        },
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      ctx.ui.notify(`Post-model-switch compaction trigger failed: ${message}`, "error");
      releaseCompactionLock();
    }
  });

  // ── Status widget showing context usage ────────────────────────────
  pi.on("turn_end", async (_event, ctx) => {
    const usage = ctx.getContextUsage();
    if (!usage || usage.tokens == null) return;

    const contextWindow = usage.contextWindow ?? 0;
    if (contextWindow <= 0) return;

    const ratio = usage.tokens / contextWindow;
    const pct = Math.round(ratio * 100);
    const filled = Math.max(0, Math.min(20, Math.round(ratio * 20)));
    const bar = "█".repeat(filled) + "░".repeat(20 - filled);

    ctx.ui.setStatus(
      "context-usage",
      `ctx: ${bar} ${pct}% (${Math.round(usage.tokens / 1000)}k/${Math.round(contextWindow / 1000)}k)`
    );
  });
}
