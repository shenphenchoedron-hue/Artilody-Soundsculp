#!/bin/sh
# Renders the HTML manuals in docs/manual to PDF files in docs/pdf using headless Chrome/Chromium.
set -e
cd "$(dirname "$0")/.."
CHROME=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
mkdir -p docs/pdf
for lang in da en; do
  upper=$(echo "$lang" | tr a-z A-Z)
  "$CHROME" --headless --disable-gpu --no-pdf-header-footer --no-sandbox \
    --print-to-pdf="docs/pdf/Artilody-Soundsculp-Manual-${upper}.pdf" \
    "file://$(pwd)/docs/manual/manual_${lang}.html" 2>/dev/null
done
ls -la docs/pdf
