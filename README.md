# Cricket JSON Data Custom Compression

![C++](https://img.shields.io/badge/C++-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Compression](https://img.shields.io/badge/Custom%20Compression-CJDC2-111827?style=flat-square)
![Lossless](https://img.shields.io/badge/Lossless-Round%20Trip-success?style=flat-square)
![Flask](https://img.shields.io/badge/Web%20UI-Flask-000000?style=flat-square&logo=flask&logoColor=white)
![Brotli](https://img.shields.io/badge/Compared%20With-Brotli-blue?style=flat-square)

A domain-specific lossless compressor for Cricsheet cricket JSON files.

This project converts repetitive cricket match JSON into a compact custom binary format called **CJDC2**.  
The core ball-by-ball stream is encoded as **one byte per delivery**, while repeated names, teams, innings, overs, wickets, reviews, replacements, and unusual delivery data are stored in compact metadata sections.

The goal is not just to compress text, but to exploit the structure of cricket data itself.

---

## Why This Project Exists

Generic compressors like Brotli are very good, but they do not understand cricket.

Cricket JSON contains a lot of repeated structure:

- player names repeated across deliveries
- team names repeated across innings
- predictable striker/non-striker/bowler state
- small run values
- recurring extras patterns
- structured wicket data
- mostly repetitive delivery objects

CJDC2 uses cricket-specific assumptions to represent the match in a smaller binary form while still preserving exact lossless decompression.

---

## Results

Across the `20` sample files in `cric/`:

| Format | Size |
| --- | ---: |
| Raw JSON | `5860.83 KB` |
| CJDC2 | `73.31 KB` |
| Raw JSON + Brotli | `90.91 KB` |
| CJDC2 + Brotli | `39.38 KB` |

Example file: `cric/1461915.json`

| Format | Size |
| --- | ---: |
| Raw JSON | `650.76 KB` |
| CJDC2 | `6.00 KB` |
| Raw JSON + Brotli | `7.65 KB` |
| CJDC2 + Brotli | `2.63 KB` |

All local samples round-trip byte-for-byte.

---

## What Makes It Interesting

This is not a wrapper around an existing compression library.

The project implements a custom cricket-aware binary format with:

- one-byte delivery encoding
- string/person/team tables
- innings and over state reconstruction
- wicket sidecars
- extras codebooks
- fallback paths for unusual deliveries
- checksum and byte-length validation
- exact round-trip decompression
- Brotli comparison
- CLI tools
- Flask web interface

---

## Architecture

```text
Cricsheet JSON
      │
      ▼
Parser
      │
      ├── Extract repeated strings
      ├── Build player/team/person tables
      ├── Analyze innings and overs
      ├── Encode predictable delivery state
      ├── Move uncommon cases into sidecars
      │
      ▼
CJDC2 Binary File
      │
      ├── Header + checksum
      ├── String/person/team tables
      ├── Match metadata
      ├── Innings/over state
      ├── Extras codebook
      ├── One-byte ball stream
      ├── Wicket sidecars
      └── Extended delivery sidecars
      │
      ▼
Decoder
      │
      ├── Validate checksum and size
      ├── Rebuild match state
      ├── Reconstruct deliveries
      └── Emit original JSON
```

---

## CJDC2 Format Overview

Current file magic:

```text
CJDC2
```

Each `.cjdc` file stores:

- original byte length
- original checksum
- string table
- person table
- team table
- match metadata
- innings and over state
- extras codebook
- one-byte ball stream
- wicket sidecars
- extended delivery sidecars

---

## One-Byte Ball Stream

Each delivery gets one byte in the main stream.

| Bits | Meaning |
| --- | --- |
| `7` | wicket present |
| `6` | extended sidecar present |
| `5..3` | batter runs code |
| `2..0` | extras code |

### Run Codes

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

### Extras Codes

| Code | Meaning |
| --- | --- |
| `0` | no extras |
| `1..6` | top six extras objects for that match |
| `7` | sidecar |

The main stream size always equals the number of deliveries.

---

## Metadata Tables

Repeated text is stored once and referenced by small integer IDs.

Examples:

- player names
- team names
- official names
- venue text
- event text
- common JSON keys and values

This avoids repeating long strings for every delivery.

---

## Over State Reconstruction

Cricket deliveries are partly predictable.

Instead of storing batter, bowler, and non-striker names for every ball, CJDC2 stores over-level state:

- over number
- delivery count
- starting batter
- bowler
- starting non-striker

The decoder follows cricket strike rules using `runs.total`.

If a delivery does not match the predicted state, an extended sidecar stores the exact player IDs.

---

## Wicket Sidecars

Wickets are stored as typed records instead of full JSON blobs.

They include:

- ball index
- player out
- dismissal kind
- fielders
- substitute flag

Known wicket kinds use compact codes. Unknown shapes fall back to exact JSON for that wicket only.

---

## Extended Delivery Sidecars

Extended sidecars handle delivery details that do not fit inside the one-byte stream.

Typed fields include:

- player override
- runs override
- extras override
- replacements
- review
- `runs.non_boundary`

Unknown delivery shapes can fall back to exact JSON for that delivery.

For the current local canonical samples, the compressor uses `0` exact delivery fallbacks.

---

## Canonical vs Noncanonical JSON

Canonical Cricsheet JSON uses the compact CJDC2 path.

Noncanonical JSON means the content may be equivalent but formatting differs, such as indentation, spacing, or trailing newlines.

CJDC2 still supports exact round-trip output by storing raw original bytes as a correctness fallback when needed.

---

## Repository Layout

```text
.
├── compress.cpp              # CLI compressor
├── decompress.cpp            # CLI decompressor
├── include/cjdc_codec.hpp    # public codec API
├── src/cjdc_codec.cpp        # CJDC2 implementation
├── scripts/roundtrip.sh      # round-trip test script
├── cric/                     # sample Cricsheet JSON files
├── app.py                    # Flask wrapper
├── templates/                # web UI templates
└── third_party/nlohmann/     # vendored JSON header
```

---

## Build

```bash
make
```

This builds:

```text
./compress
./decompress
```

The project vendors `nlohmann/json.hpp` under `third_party/`, so no system JSON package is required.

---

## CLI Usage

Compress a JSON file:

```bash
./compress cric/1461915.json /tmp/1461915.cjdc
```

Decompress a CJDC2 file:

```bash
./decompress /tmp/1461915.cjdc /tmp/1461915.json
```

Verify exact round-trip output:

```bash
cmp -s cric/1461915.json /tmp/1461915.json && echo "exact match"
```

Clean generated binaries:

```bash
make clean
```

---

## Tests

```bash
make test
```

The test script:

- compresses every local sample JSON
- decompresses every generated `.cjdc` file
- compares output with `cmp`
- verifies exact byte-for-byte round trips
- requires zero exact delivery fallbacks for local canonical samples
- verifies noncanonical JSON can round-trip through raw fallback
- verifies corrupted `.cjdc` files fail decode

---

## Web App

Build the C++ tools first:

```bash
make
```

Create a Python environment and run Flask:

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

- `.json` files for compression
- `.cjdc` files for decompression

Generated uploads and outputs are ignored by git.

---

## Correctness Guarantees

CJDC2 is designed as a lossless format.

The decoder validates:

- file magic
- original byte length
- checksum
- encoded structure
- corrupted file failure cases

A successful decompression should recreate the original file exactly.

---

## Limitations

Current limitations:

- optimized around Cricsheet-style cricket JSON
- local test set is small
- format is experimental and not standardized
- some noncanonical inputs may use raw fallback instead of compact encoding
- compression ratio depends on how closely the input follows expected Cricsheet structure

---

## Future Improvements

- add more Cricsheet samples across formats and match types
- add benchmark script for large-scale dataset testing
- document the binary format byte-by-byte
- add fuzz tests for corrupted/partial `.cjdc` files
- add CI for build and round-trip tests
- add optional Brotli integration directly in the CLI
- expose compression stats in JSON format
- create a small format specification document under `docs/`

---

## Suggested Repo Description

```text
C++ domain-specific lossless compressor for Cricsheet cricket JSON using a custom CJDC2 binary format, one-byte delivery encoding, sidecars, tests, and Flask UI.
```

---

## Suggested Topics

```text
cpp
compression
lossless-compression
binary-format
cricket
cricsheet
json
brotli
flask
systems-programming
```

---

## License

Add a license before treating this as a reusable public project.
