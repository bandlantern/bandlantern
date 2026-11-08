import type { ComponentChildren } from "preact";

interface PanelProps {
  title: string;
  class?: string;
  children: ComponentChildren;
}

/** A flat titled panel. Its header shows the active state (accent bar) while focus is inside it. */
export function Panel({ title, class: extra = "", children }: PanelProps) {
  return (
    <section class={`panel ${extra}`} aria-label={title}>
      <header class="panel-header">
        <span class="panel-title">{title}</span>
      </header>
      <div class="panel-body">{children}</div>
    </section>
  );
}
