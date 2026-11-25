import { runsForLab } from "@/lib/runs";
import { findLab } from "@/lib/labs";

export const dynamic = "force-dynamic";

export async function GET(_request: Request, context: { params: Promise<{ id: string }> }) {
  const { id } = await context.params;
  if (!findLab(id)) {
    return Response.json({ error: "lab not found" }, { status: 404 });
  }
  const runs = await runsForLab(id);
  return Response.json({ runs });
}
