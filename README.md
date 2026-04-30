# Cricket JSON Data Custom Compression

Lossless compressor for Cricsheet cricket JSON files.

It converts repetitive match JSON into a compact `.cjdc` file. The core ball stream is exactly one byte per delivery, while names, teams, innings, overs, wickets, reviews, replacements, and unusual delivery data live in compact metadata sections.

## Current Results

Across the `20` sample files in `cric/`:

| Format | Size |
| --- | ---: |
| Raw JSON | `5860.83 KB` |
| CJDC2 | `73.31 KB` |
| Raw JSON + Brotli | `90.91 KB` |
| CJDC2 + Brotli | `39.38 KB` |

Example `cric/1461915.json`:

| Format | Size |
| --- | ---: |
| Raw JSON | `650.76 KB` |
| CJDC2 | `6.00 KB` |
| Raw JSON + Brotli | `7.65 KB` |
| CJDC2 + Brotli | `2.63 KB` |

All local samples round-trip byte-for-byte.

## Build

```bash
make
```

This builds:

- `./compress`
- `./decompress`

The project vendors `nlohmann/json.hpp` under `third_party/`, so no system JSON package is required.

## CLI Usage

Compress:

```bash
./compress cric/1461915.json /tmp/1461915.cjdc
```

Decompress:

```bash
./decompress /tmp/1461915.cjdc /tmp/1461915.json
```

Verify exact match:

```bash
cmp -s cric/1461915.json /tmp/1461915.json && echo "exact match"
```

Clean generated binaries:

```bash
make clean
```

## Test

```bash
make test
```

The test script:

- Compresses every local sample JSON.
- Decompresses it.
- Compares output with `cmp`.
- Requires zero exact delivery fallbacks for local canonical samples.
- Verifies noncanonical JSON can round-trip through raw fallback.
- Verifies corrupted `.cjdc` fails decode.

## Web App

Build the C++ tools first:

```bash
make
```

Run Flask:

```bash
python3 -m venv venv
source venv/bin/activate
pip install Flask
flask --app app run
```

Open:

```text
http://127.0.0.1:5000/
```

The web UI accepts:

- `.json` for compression
- `.cjdc` for decompression

Generated uploads and outputs are ignored by git.

## How CJDC2 Works

### Container

Current file magic: `CJDC2`.

Each `.cjdc` file stores:

- Original byte length.
- Original checksum.
- String table.
- Person table.
- Team table.
- Match metadata.
- Innings and over state.
- Extras codebook.
- One-byte ball stream.
- Wicket sidecars.
- Extended delivery sidecars.

### Ball Byte

Each delivery gets one byte in the main stream.

| Bits | Meaning |
| --- | --- |
| `7` | wicket present |
| `6` | extended sidecar present |
| `5..3` | batter runs code |
| `2..0` | extras code |

Run codes:

| Code | Runs |
| --- | --- |
| `0` | `0` |
| `1` | `1` |
| `2` | `2` |
| `3` | `3` |
| `4` | `4` |
| `5` | `6` |
| `6` | `5` |
| `7` | sidecar |

Extras codes:

- `0`: no extras
- `1..6`: top six extras objects for that match
- `7`: sidecar

### Tables

Repeated text is stored once:

- player names
- team names
- official names
- venue and event text
- JSON keys and common string values

Deliveries refer to people and text by small integer IDs.

### Over State

Names are not repeated for every ball.

For each over, CJDC2 stores:

- over number
- delivery count
- starting batter
- bowler
- starting non-striker

The decoder follows cricket strike rules using `runs.total`. If the actual delivery does not match predicted state, an extended sidecar stores the exact player IDs.

### Wicket Sidecars

Wickets are typed records, not full JSON delivery blobs.

They store:

- ball index
- player out
- dismissal kind
- fielders
- substitute flag

Known wicket kinds use small codes. Unknown wicket shapes fall back to exact JSON for that wicket only.

### Extended Sidecars

Extended sidecars cover delivery details that do not fit in the byte.

Typed fields include:

- player override
- runs override
- extras override
- replacements
- review
- `runs.non_boundary`

Unknown delivery shapes fall back to exact JSON for that delivery only. Current local samples use `0` exact delivery fallbacks.

### Canonical vs Noncanonical JSON

Canonical Cricsheet JSON uses the compact CJDC2 path.

Noncanonical JSON means formatting differs from the local Cricsheet style, such as different indentation or trailing newlines. It still round-trips exactly, but CJDC2 stores raw original bytes as a correctness fallback.

## Repository Layout

```text
.
├── compress.cpp              # CLI compressor
├── decompress.cpp            # CLI decompressor
├── include/cjdc_codec.hpp    # public codec API
├── src/cjdc_codec.cpp        # CJDC2 implementation
├── scripts/roundtrip.sh      # test script
├── cric/                     # sample Cricsheet JSON files
├── app.py                    # Flask wrapper
├── templates/                # web UI templates
└── third_party/nlohmann/     # vendored JSON header
```

## Notes

- `.cjdc` is the project format.
- Brotli is optional and can be applied on top of `.cjdc`.
- Main stream size always equals delivery count.
- Decompression validates byte length and checksum before success.
