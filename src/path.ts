export function normalizeSmbPath(path: string): string {
  const trimmed = path.trim();
  if (trimmed === "" || trimmed === "/" || trimmed === "\\") {
    return "";
  }

  return trimmed
    .replaceAll("/", "\\")
    .replace(/^\\+/, "")
    .replace(/\\+/g, "\\");
}
