#!/usr/bin/env python3
"""Generate an SDR++Brown bandplan from Traficom's frequency allocation table."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any, Iterable
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urljoin
from urllib.request import Request, urlopen

API_URL = "https://opendata.traficom.fi/api/v13/Taajuusjakotaulukko"
SOURCE_URL = "https://opendata.traficom.fi/swagger/ui/index#!/Taajuusjakotaulukko/Taajuusjakotaulukko_GetTaajuusjakotaulukko"

SELECT_FIELDS = [
    "Frequency_band_lower_limit",
    "Frequency_band_upper_limit",
    "Services_in_Finland",
    "Sub_band_lower_limit",
    "Sub_band_upper_limit",
    "Sub_band_lower_limit__Hz_",
    "Sub_band_upper_limit__Hz_",
    "Sub_band_usage",
    "Direction",
    "Bandwidth",
]

# SDR++Brown has configured colors for these five type names. Unknown types are
# still valid and are rendered white, so unmatched Traficom allocations become
# "other" rather than being given a misleading service category.
TYPE_RULES: list[tuple[str, tuple[str, ...]]] = [
    ("amateur", ("amateur", "ham radio", "radio amateur")),
    ("aviation", ("aeronautical", "aviation", "aircraft", "air traffic")),
    ("marine", ("maritime", "marine", "ship station", "port operations")),
    ("broadcast", ("broadcast", "broadcasting")),
    ("military", ("military", "defence", "defense", "armed forces")),
]

# Labels in the Traficom table are written for a regulatory table rather than
# a spectrum display. Keep the meaning, but make common service names compact
# enough to be useful on SDR++Brown's waterfall.
LABEL_REPLACEMENTS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bearth exploration[- ]satellite\b", re.I), "Earth-expl. sat"),
    (re.compile(r"\bstandard frequency and time signal[- ]satellite\b", re.I), "Time/freq sat"),
    (re.compile(r"\bstandard frequency and time signal\b", re.I), "Time/freq"),
    (re.compile(r"\bbroadcasting[- ]satellite\b", re.I), "Broadcast sat"),
    (re.compile(r"\bfixed[- ]satellite\b", re.I), "Fixed sat"),
    (re.compile(r"\bmobile[- ]satellite\b", re.I), "Mobile sat"),
    (re.compile(r"\bamateur[- ]satellite\b", re.I), "Amateur sat"),
    (re.compile(r"\bmeteorological[- ]satellite\b", re.I), "Met sat"),
    (re.compile(r"\binter[- ]satellite\b", re.I), "Inter-sat"),
    (re.compile(r"\baeronautical\b", re.I), "Aero"),
    (re.compile(r"\bmaritime\b", re.I), "Maritime"),
    (re.compile(r"\bmeteorological\b", re.I), "Met"),
    (re.compile(r"\bradionavigation\b", re.I), "Radionav"),
    (re.compile(r"\bradiodetermination\b", re.I), "Radiodeterm."),
    (re.compile(r"\bradio astronomy\b", re.I), "Radio astronomy"),
    (re.compile(r"\bexcept\b", re.I), "excl."),
]

_SPACE_AROUND_PUNCT_RE = re.compile(r"\s*([;,])\s*")
_MULTISPACE_RE = re.compile(r"\s+")

_FREQ_RE = re.compile(
    r"([0-9]+(?:[.,][0-9]+)?)\s*(hz|khz|mhz|ghz|thz)\b",
    re.IGNORECASE,
)
_FREQ_MULTIPLIER = {
    "hz": Decimal(1),
    "khz": Decimal(1_000),
    "mhz": Decimal(1_000_000),
    "ghz": Decimal(1_000_000_000),
    "thz": Decimal(1_000_000_000_000),
}


def clean(value: Any) -> str:
    if value is None:
        return ""
    return " ".join(str(value).replace("\u00a0", " ").split())


def compact_label(value: Any, max_length: int = 48) -> str:
    """Turn a regulatory-table label into a compact waterfall label."""
    text = clean(value)
    if not text:
        return ""

    if any(c.isalpha() for c in text) and text == text.upper():
        text = text.title()

    for pattern, replacement in LABEL_REPLACEMENTS:
        text = pattern.sub(replacement, text)

    text = re.sub(r'^\(\w+\) ', '', text)
    text = _SPACE_AROUND_PUNCT_RE.sub(r"\1 ", text)
    text = _MULTISPACE_RE.sub(" ", text).strip(" -;,/")

    if len(text) > max_length:
        text = text[: max(1, max_length - 1)].rstrip(" -;,/") + "..."
    return text


def numeric_hz(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(Decimal(str(value)))
    except (InvalidOperation, ValueError):
        return None


def parse_frequency_text(value: Any) -> int | None:
    text = clean(value)
    match = _FREQ_RE.search(text)
    if not match:
        return None
    number = match.group(1).replace(",", ".")
    unit = match.group(2).lower()
    try:
        return int(Decimal(number) * _FREQ_MULTIPLIER[unit])
    except (InvalidOperation, ValueError):
        return None


def row_bounds(row: dict[str, Any]) -> tuple[int | None, int | None]:
    # Prefer Traficom's already-normalized sub-band bounds in Hz.
    start = numeric_hz(row.get("Sub_band_lower_limit__Hz_"))
    end = numeric_hz(row.get("Sub_band_upper_limit__Hz_"))

    # Fall back to textual sub-band limits, then the parent frequency band.
    if start is None:
        start = parse_frequency_text(row.get("Sub_band_lower_limit"))
    if end is None:
        end = parse_frequency_text(row.get("Sub_band_upper_limit"))
    if start is None:
        start = parse_frequency_text(row.get("Frequency_band_lower_limit"))
    if end is None:
        end = parse_frequency_text(row.get("Frequency_band_upper_limit"))
    return start, end


def classify_type(row: dict[str, Any]) -> str:
    haystack = " ".join(
        clean(row.get(k)).casefold()
        for k in ("Services_in_Finland", "Sub_band_usage")
    )
    for band_type, needles in TYPE_RULES:
        if any(needle in haystack for needle in needles):
            return band_type
    return "other"


def _dedupe_names(names: Iterable[str]) -> list[str]:
    unique: list[str] = []
    seen: set[str] = set()
    for name in names:
        key = name.casefold()
        if not key or key in seen:
            continue
        seen.add(key)
        unique.append(name)

    return unique


def combine_names(names: Iterable[str], max_length: int) -> str:
    names = _dedupe_names(names)
    if not names:
        return "Allocation"
    if len(names) == 1:
        return names[0]

    ordered = sorted(names, key=lambda n: (len(n), n.casefold()))
    parts: list[str] = []
    for i, name in enumerate(ordered):
        remaining_after = len(ordered) - i - 1
        candidate = " / ".join(parts + [name])
        suffix = f" +{remaining_after}" if remaining_after else ""
        if len(candidate + suffix) <= max_length:
            parts.append(name)
            continue
        break

    if not parts:
        return compact_label(ordered[0], max_length=max_length)
    result = " / ".join(parts)
    return result


def _compatible_merge_type(left_type: str, right_type: str) -> str | None:
    """Return the merged type, or None when the types must stay separate."""
    if left_type == right_type:
        return left_type
    if left_type == "other":
        return right_type
    if right_type == "other":
        return left_type
    return None


def merge_bands(
    bands: list[dict[str, Any]],
    max_label_length: int,
    merge_touching_hz: int = 0,
) -> list[dict[str, Any]]:
    """Merge same-name overlapping/connected allocations when types allow it.

    Bands are merged when all of the following are true:
      * their names are equal, case-insensitively;
      * their frequency ranges overlap, touch, or are separated by no more than
        ``merge_touching_hz``; and
      * their types are equal, or either type is ``other``.

    When ``other`` is merged with a specific Brown type, the specific type wins.
    Different specific types are never merged. This removes duplicate pieces of
    the same named allocation without inventing a mixed color/category.
    """
    del max_label_length  # Kept in the signature for CLI/backward compatibility.

    by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for band in bands:
        by_name[band["name"].casefold()].append(dict(band))

    merged: list[dict[str, Any]] = []

    for group in by_name.values():
        # Sorting by start makes overlapping/touching runs adjacent. Put specific
        # types before "other" at the same start so the resulting type settles
        # on the useful Brown color as early as possible.
        group.sort(
            key=lambda b: (
                int(b["start"]),
                int(b["end"]),
                b["type"] == "other",
                b["type"],
            )
        )

        name_merged: list[dict[str, Any]] = []
        for band in group:
            candidate = dict(band)

            # A band may be compatible with an earlier interval even when an
            # incompatible specific-type interval sits between them in the list.
            # Search backwards through intervals that can still geometrically
            # connect to this one.
            merge_index: int | None = None
            merged_type: str | None = None
            for i in range(len(name_merged) - 1, -1, -1):
                prev = name_merged[i]
                if int(prev["end"]) + merge_touching_hz < int(candidate["start"]):
                    break

                compatible_type = _compatible_merge_type(prev["type"], candidate["type"])
                if compatible_type is not None:
                    merge_index = i
                    merged_type = compatible_type
                    break

            if merge_index is None:
                name_merged.append(candidate)
                continue

            prev = name_merged[merge_index]
            prev["start"] = min(int(prev["start"]), int(candidate["start"]))
            prev["end"] = max(int(prev["end"]), int(candidate["end"]))
            prev["type"] = merged_type

            # The enlarged interval can now connect to later same-name intervals
            # that were previously separate. Fold those in transitively whenever
            # the type rule still permits it.
            j = merge_index + 1
            while j < len(name_merged):
                other = name_merged[j]
                if int(other["start"]) > int(prev["end"]) + merge_touching_hz:
                    break
                compatible_type = _compatible_merge_type(prev["type"], other["type"])
                if compatible_type is None:
                    j += 1
                    continue
                prev["end"] = max(int(prev["end"]), int(other["end"]))
                prev["type"] = compatible_type
                del name_merged[j]

        merged.extend(name_merged)

    merged.sort(key=lambda b: (b["start"], b["end"], b["type"], b["name"].casefold()))
    return merged


def build_first_url(api_url: str) -> str:
    query = urlencode({"$select": ",".join(SELECT_FIELDS)}, safe=",")
    separator = "&" if "?" in api_url else "?"
    return f"{api_url}{separator}{query}"


def fetch_json(url: str, timeout: float) -> Any:
    request = Request(
        url,
        headers={
            "Accept": "application/json",
            "User-Agent": "Traficom-to-SDRPlusPlusBrown/1.0",
        },
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            return json.loads(response.read().decode(charset))
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Traficom returned HTTP {exc.code}: {body[:500]}") from exc
    except URLError as exc:
        raise RuntimeError(f"Could not reach Traficom: {exc.reason}") from exc


def fetch_all_rows(api_url: str, timeout: float) -> list[dict[str, Any]]:
    url = build_first_url(api_url)
    rows: list[dict[str, Any]] = []
    page = 0

    while url:
        page += 1
        payload = fetch_json(url, timeout)

        if isinstance(payload, list):
            page_rows = payload
            next_url = None
        elif isinstance(payload, dict):
            page_rows = payload.get("value", [])
            next_url = (
                payload.get("@odata.nextLink")
                or payload.get("odata.nextLink")
                or payload.get("nextLink")
            )
        else:
            raise RuntimeError(f"Unexpected Traficom response type: {type(payload).__name__}")

        if not isinstance(page_rows, list):
            raise RuntimeError("Traficom response did not contain an OData 'value' array")

        rows.extend(r for r in page_rows if isinstance(r, dict))
        print(f"Fetched page {page}: {len(page_rows)} rows ({len(rows)} total)", file=sys.stderr)
        url = urljoin(url, next_url) if next_url else ""

    return rows


def convert_rows(
    rows: Iterable[dict[str, Any]],
    min_hz: int | None,
    max_hz: int | None,
    verbose_names: bool,
    max_label_length: int,
    merge: bool,
    merge_touching_hz: int,
) -> tuple[list[dict[str, Any]], int]:
    bands: list[dict[str, Any]] = []
    skipped = 0
    seen: set[tuple[str, str, int, int]] = set()

    for row in rows:
        start, end = row_bounds(row)
        if start is None or end is None or end <= start:
            skipped += 1
            continue
        if min_hz is not None and end < min_hz:
            continue
        if max_hz is not None and start > max_hz:
            continue

        name = compact_label(row.get("Sub_band_usage"), max_length=max_label_length)
        band_type = classify_type(row)
        key = (name, band_type, start, end)
        if key in seen:
            continue
        seen.add(key)

        bands.append({
            "name": name,
            "type": band_type,
            "start": start,
            "end": end,
        })

    if merge:
        bands = merge_bands(
            bands,
            max_label_length=max_label_length,
            merge_touching_hz=merge_touching_hz,
        )
    else:
        bands.sort(key=lambda b: (b["start"], b["end"], b["name"].casefold()))
    return bands, skipped


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an SDR++Brown bandplan JSON from Traficom open data."
    )
    parser.add_argument("-o", "--output", type=Path, default=Path("finland-traficom.json"))
    parser.add_argument("--api-url", default=API_URL, help="Traficom OData endpoint")
    parser.add_argument("--min-hz", type=int, default=None, help="Keep bands intersecting this lower frequency")
    parser.add_argument("--max-hz", type=int, default=None, help="Keep bands intersecting this upper frequency")
    parser.add_argument("--verbose-names", action="store_true", help="Add traffic/direction/bandwidth details to labels")
    parser.add_argument(
        "--max-label-length",
        type=int,
        default=48,
        help="Maximum waterfall label length (default: 48)",
    )
    parser.add_argument(
        "--merge",
        action="store_true",
        help="Flatten/combine overlaps",
    )
    parser.add_argument(
        "--merge-touching-hz",
        type=int,
        default=0,
        help="Merge same-name/type bands separated by at most this many Hz (default: 0)",
    )
    parser.add_argument("--timeout", type=float, default=30.0, help="HTTP timeout per request in seconds")
    args = parser.parse_args()

    if args.min_hz is not None and args.max_hz is not None and args.min_hz > args.max_hz:
        parser.error("--min-hz must not be greater than --max-hz")
    if args.max_label_length < 12:
        parser.error("--max-label-length must be at least 12")
    if args.merge_touching_hz < 0:
        parser.error("--merge-touching-hz must not be negative")

    rows = fetch_all_rows(args.api_url, args.timeout)
    bands, skipped = convert_rows(
        rows,
        args.min_hz,
        args.max_hz,
        args.verbose_names,
        args.max_label_length,
        merge=args.merge,
        merge_touching_hz=args.merge_touching_hz,
    )

    plan = {
        "name": "Finland - Traficom",
        "country_name": "Finland",
        "country_code": "FI",
        "author_name": "Traficom open data",
        "author_url": SOURCE_URL,
        "bands": bands,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as fh:
        json.dump(plan, fh, ensure_ascii=False, indent=4)
        fh.write("\n")

    print(f"Wrote {len(bands)} bands to {args.output}")
    if skipped:
        print(f"Skipped {skipped} rows with missing/invalid frequency bounds", file=sys.stderr)
    print("Unmatched service categories use type 'other' (SDR++Brown renders unknown types white).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())