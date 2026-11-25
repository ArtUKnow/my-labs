import { runLabTests } from "@/lib/runs";
import { LABS } from "@/lib/labs";

export const dynamic = "force-dynamic";
export const maxDuration = 300;

export async function POST() {
  const results = [];
  for (const lab of LABS) {
    const result = await runLabTests(lab.id);
    results.push({ labId: lab.id, status: result.run.status, passed: result.run.passed, total: result.run.total });
  }
  return Response.json({ results });
}
