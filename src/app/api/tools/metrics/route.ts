import { runPython, LABS_ROOT } from "@/lib/python";
import path from "node:path";

export const dynamic = "force-dynamic";

export async function POST(request: Request) {
  const body = await request.json();
  const run = String(body.run ?? "").slice(0, 20000);
  const result = await runPython(["evaluate.py", "--stdin"], { cwd: path.join(LABS_ROOT, "lab06_quality"), input: run });
  return Response.json({ output: result.stdout, error: result.stderr || null });
}
