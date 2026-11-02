import { defineCollection, z } from "astro:content";
import { glob } from "astro/loaders";

// One post per curriculum module (docs/learning/CURRICULUM.md), plus post 0.
const posts = defineCollection({
  loader: glob({ pattern: "**/*.md", base: "./src/content/posts" }),
  schema: z.object({
    title: z.string(),
    date: z.coerce.date(),
    module: z.string().optional(),
    summary: z.string(),
    draft: z.boolean().default(false),
  }),
});

export const collections = { posts };
