import { runPython, LABS_ROOT } from "@/lib/python";
import path from "node:path";

export const dynamic = "force-dynamic";

export async function POST(request: Request) {
  const body = await request.json();
  const text = String(body.text ?? "").slice(0, 5000);
  const cwd = path.join(LABS_ROOT, "lab04_stemmer");
  const tokensResult = await runPython(["tokenizer.py"], { cwd: path.join(LABS_ROOT, "lab03_tokenizer"), input: text });
  const words = tokensResult.stdout.split("\n").filter((t) => t !== "");
  const input = words.join("\n") + "\n";
  const [stems, lemmas] = await Promise.all([
    runPython(["stemmer.py"], { cwd, input }),
    runPython(["lemmatizer.py"], { cwd, input }),
  ]);
  const stemList = stems.stdout.split("\n");
  const lemmaList = lemmas.stdout.split("\n");
  const rows = words.map((word, i) => ({ word, stem: stemList[i] ?? "", lemma: lemmaList[i] ?? "" }));
  return Response.json({ rows, error: stems.stderr || lemmas.stderr || null });
}
