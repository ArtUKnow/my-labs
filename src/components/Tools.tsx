"use client";

import { useState } from "react";

type Tool = "tokenize" | "stem" | "search" | "metrics";

const TOOL_TITLES: Record<Tool, string> = {
  tokenize: "Токенизатор (лаба 3)",
  stem: "Стеммер и лемматизатор (лаба 4)",
  search: "Булев поиск по индексу (лаба 7)",
  metrics: "Калькулятор метрик (лаба 6)",
};

const DEFAULTS: Record<Tool, string> = {
  tokenize: "12 апреля 1961 года ракета-носитель «Восток» вывела корабль на орбиту. Скорость 7,9 км/с — don't forget!",
  stem: "ракеты летали на орбите, космонавты выходили в открытый космос, спутников стало больше, rockets were launched",
  search: "космический мусор",
  metrics: "q1\tru_wikipedia/Ракета\t1\nq1\tru_wikipedia/Космонавтика\t2\nq2\tru_wikipedia/Невесомость\t1\nq3\tsimple_wikipedia/Kessler_Syndrome\t1",
};

type TokenizeResult = { tokens: string[]; count: number };
type StemResult = { rows: { word: string; stem: string; lemma: string }[] };
type SearchResult = { query: string; terms: { token: string; term: string; df: number }[]; count: number; documents: string[] };
type MetricsResult = { output: string; error: string | null };

export function Tools({ only }: { only?: Tool[] }) {
  const tools: Tool[] = only ?? ["tokenize", "stem", "search", "metrics"];
  const [tool, setTool] = useState<Tool>(tools[0]);
  const [text, setText] = useState(DEFAULTS[tools[0]]);
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<TokenizeResult | StemResult | SearchResult | MetricsResult | null>(null);
  const [error, setError] = useState<string | null>(null);

  function switchTool(next: Tool) {
    setTool(next);
    setText(DEFAULTS[next]);
    setResult(null);
    setError(null);
  }

  async function submit() {
    setLoading(true);
    setError(null);
    try {
      const body = tool === "search" ? { query: text } : tool === "metrics" ? { run: text } : { text };
      const response = await fetch(`/api/tools/${tool}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      const data = await response.json();
      if (data.error && tool !== "metrics") setError(String(data.error));
      setResult(data);
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="rounded-2xl border border-slate-200 bg-white p-6 shadow-sm">
      {tools.length > 1 && (
        <div className="mb-4 flex flex-wrap gap-2">
          {tools.map((t) => (
            <button
              key={t}
              onClick={() => switchTool(t)}
              className={`rounded-full px-3 py-1 text-sm transition ${tool === t ? "bg-slate-900 text-white" : "bg-slate-100 text-slate-700 hover:bg-slate-200"}`}
            >
              {TOOL_TITLES[t]}
            </button>
          ))}
        </div>
      )}
      <h3 className="text-base font-semibold text-slate-900">{TOOL_TITLES[tool]}</h3>
      <p className="mt-1 text-sm text-slate-500">
        {tool === "tokenize" && "Текст передаётся в tokenizer.py через stdin, результат — список токенов."}
        {tool === "stem" && "Слова прогоняются через stemmer.py и lemmatizer.py (словарь форм из корпуса)."}
        {tool === "search" && "Запрос нормализуется и ищется пересечением списков постингов (AND) в бинарном индексе."}
        {tool === "metrics" && "Формат строки: query_id <TAB> doc_id <TAB> rank. Метрики считаются по qrels корпуса."}
      </p>
      <textarea
        value={text}
        onChange={(e) => setText(e.target.value)}
        rows={tool === "metrics" ? 5 : 3}
        className="mt-3 w-full rounded-lg border border-slate-300 p-3 font-mono text-sm focus:border-indigo-500 focus:outline-none"
      />
      <button
        onClick={submit}
        disabled={loading}
        className="mt-3 rounded-lg bg-slate-900 px-4 py-2 text-sm font-medium text-white hover:bg-slate-700 disabled:opacity-60"
      >
        {loading ? "Выполняется…" : "Выполнить"}
      </button>
      {error && <pre className="mt-3 overflow-auto rounded-lg bg-red-50 p-3 text-xs text-red-700">{error}</pre>}
      {result && tool === "tokenize" && "tokens" in result && (
        <div className="mt-4">
          <p className="text-sm text-slate-600">Токенов: {result.count}</p>
          <div className="mt-2 flex flex-wrap gap-1.5">
            {result.tokens.map((t, i) => (
              <span key={i} className="rounded bg-indigo-50 px-2 py-0.5 font-mono text-xs text-indigo-800">
                {t}
              </span>
            ))}
          </div>
        </div>
      )}
      {result && tool === "stem" && "rows" in result && (
        <table className="mt-4 w-full text-sm">
          <thead>
            <tr className="border-b text-left text-slate-500">
              <th className="py-1">слово</th>
              <th className="py-1">стем</th>
              <th className="py-1">лемма</th>
            </tr>
          </thead>
          <tbody>
            {result.rows.map((r, i) => (
              <tr key={i} className="border-b border-slate-100 font-mono">
                <td className="py-1">{r.word}</td>
                <td className="py-1 text-amber-700">{r.stem}</td>
                <td className="py-1 text-emerald-700">{r.lemma}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      {result && tool === "search" && "documents" in result && (
        <div className="mt-4 text-sm">
          <div className="flex flex-wrap gap-2">
            {result.terms.map((t, i) => (
              <span key={i} className="rounded bg-slate-100 px-2 py-0.5 font-mono text-xs">
                {t.token} → {t.term} (df={t.df})
              </span>
            ))}
          </div>
          <p className="mt-3 text-slate-600">Найдено документов: {result.count}</p>
          <ul className="mt-2 grid gap-1 sm:grid-cols-2">
            {result.documents.map((d) => (
              <li key={d} className="rounded bg-slate-50 px-2 py-1 font-mono text-xs">
                {d}
              </li>
            ))}
          </ul>
        </div>
      )}
      {result && tool === "metrics" && "output" in result && (
        <pre className="mt-4 overflow-auto rounded-lg bg-slate-900 p-3 text-xs text-slate-100">{result.output || result.error}</pre>
      )}
    </div>
  );
}
