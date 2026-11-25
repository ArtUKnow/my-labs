import { latestRuns } from "@/lib/runs";

export const dynamic = "force-dynamic";

export async function GET() {
  const items = await latestRuns();
  return Response.json({
    labs: items.map(({ lab, run }) => ({
      id: lab.id,
      number: lab.number,
      title: lab.title,
      grade: lab.grade,
      run: run ? { status: run.status, passed: run.passed, failed: run.failed, errors: run.errors, total: run.total, createdAt: run.createdAt } : null,
    })),
  });
}
