"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";

export function RunButton({ labId, label, className }: { labId?: string; label?: string; className?: string }) {
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const router = useRouter();

  async function run() {
    setLoading(true);
    setMessage(null);
    try {
      const url = labId ? `/api/labs/${labId}/run` : "/api/run-all";
      const response = await fetch(url, { method: "POST" });
      const data = await response.json();
      if (labId) {
        setMessage(`${data.run.status === "passed" ? "✓" : "✗"} ${data.run.passed}/${data.run.total} за ${(data.run.durationMs / 1000).toFixed(1)} с`);
      } else {
        const passed = data.results.filter((r: { status: string }) => r.status === "passed").length;
        setMessage(`Готово: ${passed}/${data.results.length} лаб прошли`);
      }
      router.refresh();
    } catch (error) {
      setMessage("Ошибка: " + String(error));
    } finally {
      setLoading(false);
    }
  }

  return (
    <span className="inline-flex items-center gap-3">
      <button
        onClick={run}
        disabled={loading}
        className={
          className ??
          "rounded-lg bg-indigo-600 px-4 py-2 text-sm font-medium text-white shadow-sm transition hover:bg-indigo-500 disabled:cursor-wait disabled:opacity-60"
        }
      >
        {loading ? (
          <span className="inline-flex items-center gap-2">
            <span className="h-3 w-3 animate-spin rounded-full border-2 border-white/40 border-t-white" />
            Выполняется…
          </span>
        ) : (
          label ?? "Запустить автотесты"
        )}
      </button>
      {message && <span className="text-sm text-slate-600">{message}</span>}
    </span>
  );
}
