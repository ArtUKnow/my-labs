import { runPython, LABS_ROOT } from "@/lib/python";
import path from "node:path";

export const dynamic = "force-dynamic";

export async function POST(request: Request) {
  const body = await request.json();
  const text = String(body.text ?? "").slice(0, 20000);
  const result = await runPython(["tokenizer.py"], { cwd: path.join(LABS_ROOT, "lab03_tokenizer"), input: text });
  const tokens = result.stdout.split("\n").filter((t) => t !== "");
  return Response.json({ tokens, count: tokens.length, error: result.stderr || null });
}
