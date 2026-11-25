import { runLabTests } from "@/lib/runs";
import { findLab } from "@/lib/labs";

export const dynamic = "force-dynamic";

export async function POST(_request: Request, context: { params: Promise<{ id: string }> }) {
  const { id } = await context.params;
  if (!findLab(id)) {
    return Response.json({ error: "lab not found" }, { status: 404 });
  }
  const result = await runLabTests(id);
  return Response.json(result);
}
