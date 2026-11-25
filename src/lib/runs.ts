import { db } from "@/db";
import { testRuns } from "@/db/schema";
import { desc, eq } from "drizzle-orm";
import { findLab, LABS } from "@/lib/labs";
import { LABS_ROOT, parseUnittestOutput, runPython } from "@/lib/python";
import path from "node:path";
import fs from "node:fs";

export async function runLabTests(labId: string) {
  const lab = findLab(labId);
  if (!lab) throw new Error("unknown lab");
  const result = await runPython([lab.testFile], { cwd: path.join(LABS_ROOT, lab.folder), timeoutMs: 180000 });
  const output = result.stdout + result.stderr;
  const parsed = parseUnittestOutput(output);
  const status = result.code === 0 && parsed.failed === 0 && parsed.errors === 0 ? "passed" : "failed";
  const resultsDir = path.join(LABS_ROOT, lab.folder, "results");
  try {
    fs.mkdirSync(resultsDir, { recursive: true });
    fs.writeFileSync(path.join(resultsDir, "test_report.txt"), output);
  } catch {}
  const [row] = await db
    .insert(testRuns)
    .values({
      labId,
      status,
      passed: parsed.passed,
      failed: parsed.failed,
      errors: parsed.errors,
      total: parsed.total,
      durationMs: result.durationMs,
      output,
    })
    .returning();
  return { run: row, cases: parsed.cases };
}

export async function latestRuns() {
  const rows = await db.select().from(testRuns).orderBy(desc(testRuns.createdAt));
  const latest = new Map<string, typeof rows[number]>();
  for (const row of rows) {
    if (!latest.has(row.labId)) latest.set(row.labId, row);
  }
  return LABS.map((lab) => ({ lab, run: latest.get(lab.id) ?? null }));
}

export async function runsForLab(labId: string, limit = 10) {
  return db.select().from(testRuns).where(eq(testRuns.labId, labId)).orderBy(desc(testRuns.createdAt)).limit(limit);
}
