#!/usr/bin/env bash
# Fetch a Nasdaq BX TotalView-ITCH 5.0 sample (smaller than the main feed,
# good for iteration) and decompress it. The data file is gitignored on
# purpose — re-run this to reproduce it instead of committing it.
#
# Available dates change over time. If a URL 404s, browse the directory listing
# at https://emi.nasdaq.com/ITCH/ and pick another date.
set -euo pipefail

DATE="${1:-20190130}"
FILE="${DATE}.BX_ITCH_50.gz"
URL="https://emi.nasdaq.com/ITCH/Nasdaq%20BX%20ITCH/${FILE}"

cd "$(dirname "$0")"
echo "downloading ${URL}"
curl -fL -o "${FILE}" "${URL}"
echo "decompressing ${FILE}"
gunzip -k "${FILE}"
echo "ready: data/${DATE}.BX_ITCH_50"