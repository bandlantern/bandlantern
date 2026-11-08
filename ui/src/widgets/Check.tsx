// Checkbox or radio: the native input stays (keyboard, forms, screen readers) but is drawn
// invisibly over a styled box, so the look comes from tokens instead of the OS.

interface CheckProps {
  type?: "checkbox" | "radio";
  label: string;
  name?: string;
  checked: boolean;
  disabled?: boolean;
  onChange?: (checked: boolean) => void;
}

export function Check({ type = "checkbox", label, name, checked, disabled, onChange }: CheckProps) {
  const input = {
    name,
    checked,
    disabled,
    onChange: (e: Event) => onChange?.((e.currentTarget as HTMLInputElement).checked),
  };
  return (
    <label class="check">
      {type === "radio" ? <input type="radio" {...input} /> : <input type="checkbox" {...input} />}
      <span class={`box ${type === "radio" ? "round" : ""}`} aria-hidden="true" />
      <span class="text">{label}</span>
    </label>
  );
}
