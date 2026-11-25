import { runPython, LABS_ROOT } from "@/lib/python";
import path from "node:path";
import fs from "node:fs";

export const dynamic = "force-dynamic";

export async function POST(request: Request) {
  const body = await request.json();
  const query = String(body.query ?? "").replace(/\n/g, " ").slice(0, 500);
  const indexDir = path.join(LABS_ROOT, "data", "index");
  if (!fs.existsSync(path.join(indexDir, "dictionary.txt"))) {
    const build = await runPython(["build_index.py", "../data/text", "../data/index", "stem", "10000"], { cwd: path.join(LABS_ROOT, "lab07_boolean_index") });
    if (build.code !== 0) {
      return Response.json({ error: build.stderr }, { status: 500 });
    }
  }
  const result = await runPython(["index_reader.py", "--json"], { cwd: path.join(LABS_ROOT, "lab07_boolean_index"), input: query + "\n" });
  const line = result.stdout.split("\n").find((l) => l.trim() !== "");
  if (!line) {
    return Response.json({ query, terms: [], count: 0, documents: [], error: result.stderr || null });
  }
  return Response.json(JSON.parse(line));
}
