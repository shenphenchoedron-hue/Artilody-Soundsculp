#!/bin/sh
# Renders the HTML manuals in docs/manual to PDF files in docs/pdf using headless Chrome/Chromium.
# Set CHROME=/path/to/chrome to use a specific browser binary.
set -e
cd "$(dirname "$0")/.."

find_chrome() {
  if [ -n "${CHROME:-}" ]; then
    command -v "$CHROME"
    return $?
  fi
  for c in google-chrome google-chrome-stable chromium chromium-browser chrome; do
    if command -v "$c" >/dev/null 2>&1; then
      command -v "$c"
      return 0
    fi
  done
  for c in \
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "$HOME/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "/Applications/Chromium.app/Contents/MacOS/Chromium" \
    "$HOME/Applications/Chromium.app/Contents/MacOS/Chromium"; do
    if [ -x "$c" ]; then
      printf '%s\n' "$c"
      return 0
    fi
  done
  return 1
}

if ! CHROME=$(find_chrome) || [ ! -x "$CHROME" ]; then
  echo "error: Google Chrome or Chromium not found." >&2
  echo "       Install it, or run with CHROME=/path/to/chrome $0" >&2
  exit 1
fi
echo "Using browser: $CHROME"

mkdir -p docs/pdf
for lang in da en; do
  upper=$(echo "$lang" | tr a-z A-Z)
  out="docs/pdf/Artilody-Soundsculp-Manual-${upper}.pdf"
  rm -f "$out"
  "$CHROME" --headless --disable-gpu --no-pdf-header-footer --no-sandbox \
    --print-to-pdf="$out" \
    "file://$(pwd)/docs/manual/manual_${lang}.html"
  if [ ! -s "$out" ]; then
    echo "error: $out was not created" >&2
    exit 1
  fi
done
ls -la docs/pdf
