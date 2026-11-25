import { pgTable, serial, text, integer, timestamp } from "drizzle-orm/pg-core";

export const testRuns = pgTable("test_runs", {
  id: serial("id").primaryKey(),
  labId: text("lab_id").notNull(),
  status: text("status").notNull(),
  passed: integer("passed").notNull().default(0),
  failed: integer("failed").notNull().default(0),
  errors: integer("errors").notNull().default(0),
  total: integer("total").notNull().default(0),
  durationMs: integer("duration_ms").notNull().default(0),
  output: text("output").notNull().default(""),
  createdAt: timestamp("created_at").notNull().defaultNow(),
});

export type TestRun = typeof testRuns.$inferSelect;
