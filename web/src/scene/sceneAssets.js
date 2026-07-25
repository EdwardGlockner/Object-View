// Resolves scene JSON / asset paths for both bundled public/ URLs and, in
// dev, arbitrary local filesystem paths via Vite's /@fs/ mechanism -- this
// is what lets a sibling project (e.g. latent-world/exports/*.json) be
// opened directly without copying files into web/public.

export function toFetchablePath(input) {
  const trimmed = input.trim();
  if (trimmed.startsWith("/@fs/") || trimmed.startsWith("/") || /^https?:\/\//.test(trimmed)) {
    return trimmed;
  }
  if (/^[a-zA-Z]:[\\/]/.test(trimmed)) {
    return "/@fs/" + trimmed.replace(/\\/g, "/");
  }
  return trimmed;
}

export function resolveRelative(basePath, relativePath) {
  const base = new URL(basePath, window.location.origin);
  return new URL(relativePath, base).pathname;
}

export function withExtension(path, extension) {
  return path.replace(/\.[^./\\]+$/, extension);
}
