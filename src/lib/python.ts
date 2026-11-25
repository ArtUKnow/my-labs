import { spawn } from "node:child_process";
import path from "node:path";
import fs from "node:fs";

export const LABS_ROOT = path.join(process.cwd(), "labs");

export type RunResult = {
  code: number;
  stdout: string;
  stderr: string;
  durationMs: number;
};

export function runPython(args: string[], options: { cwd?: string; input?: string; timeoutMs?: number } = {}): Promise<RunResult> {
  return new Promise((resolve) => {
    const started = Date.now();
    const child = spawn("python3", args, { cwd: options.cwd ?? LABS_ROOT });
    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => child.kill("SIGKILL"), options.timeoutMs ?? 120000);
    child.stdout.on("data", (chunk) => (stdout += chunk.toString()));
    child.stderr.on("data", (chunk) => (stderr += chunk.toString()));
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code: code ?? -1, stdout, stderr, durationMs: Date.now() - started });
    });
    child.on("error", (error) => {
      clearTimeout(timer);
      resolve({ code: -1, stdout, stderr: stderr + String(error), durationMs: Date.now() - started });
    });
    if (options.input !== undefined) {
      child.stdin.write(options.input);
    }
    child.stdin.end();
  });
}

export type TestCase = { name: string; status: "ok" | "FAIL" | "ERROR" | "skipped" };

export function parseUnittestOutput(output: string) {
  const cases: TestCase[] = [];
  const lines = output.split("\n");
  for (const line of lines) {
    const match = line.match(/^(test_\w+) \((.+?)\) \.\.\. (ok|FAIL|ERROR|skipped.*)$/);
    if (match) {
      let status: TestCase["status"] = "ok";
      if (match[3].startsWith("FAIL")) status = "FAIL";
      else if (match[3].startsWith("ERROR")) status = "ERROR";
      else if (match[3].startsWith("skipped")) status = "skipped";
      cases.push({ name: match[2].replace("__main__.", "") + "." + match[1], status });
    }
  }
  const ranMatch = output.match(/Ran (\d+) tests? in ([\d.]+)s/);
  const total = ranMatch ? Number(ranMatch[1]) : cases.length;
  const failed = cases.filter((c) => c.status === "FAIL").length;
  const errors = cases.filter((c) => c.status === "ERROR").length;
  const passed = cases.filter((c) => c.status === "ok").length;
  return { cases, total, passed, failed, errors };
}

export function readResultJson<T>(folder: string, file: string): T | null {
  const full = path.join(LABS_ROOT, folder, "results", file);
  if (!fs.existsSync(full)) return null;
  try {
    return JSON.parse(fs.readFileSync(full, "utf-8")) as T;
  } catch {
    return null;
  }
}

export function readResultText(folder: string, file: string): string | null {
  const full = path.join(LABS_ROOT, folder, "results", file);
  if (!fs.existsSync(full)) return null;
  return fs.readFileSync(full, "utf-8");
}

export function readSource(folder: string, file: string): string | null {
  const full = path.join(LABS_ROOT, folder, file);
  if (!full.startsWith(LABS_ROOT) || !fs.existsSync(full)) return null;
  return fs.readFileSync(full, "utf-8");
}
