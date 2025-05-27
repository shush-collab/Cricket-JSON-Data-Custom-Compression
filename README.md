# Cricket-JSON-Data-Custom-Compression

A Flask web app that lets you upload a cricket match JSON data, compress it (via a custom C++ 1 byte/ball algorithm + Brotli), and download the compressed `.br` file—or upload a `.br` to decompress back to JSON.

## 🚀 Features

- **Custom C++ compressor/decompressor** using [nlohmann/json](https://github.com/nlohmann/json)  
- **Brotli** lossless compression on top of the C++ output  
- **Flask**-based UI for upload/download and compression statistics  

---

## 📋 Prerequisites

1. **Git**  
   ```bash
   sudo apt update
   sudo apt install git

    C++ Build Tools

sudo apt install build-essential cmake

nlohmann/json (header‐only C++ library)

    Debian/Ubuntu:

    sudo apt install nlohmann-json3-dev

    Or download the single header from
    https://github.com/nlohmann/json/releases/latest/download/json.hpp
    and place it in your C++ include path (e.g. include/nlohmann/json.hpp).

Brotli CLI

sudo apt install brotli

Python 3.10+ & venv

    sudo apt install python3 python3-venv python3-pip

🛠️ Setup Instructions

# 1) Clone your repo
git clone https://github.com/<YOUR_USERNAME>/Cricket-JSON-Data-Custom-Compression.git
cd Cricket-JSON-Data-Custom-Compression

# 2) Build the C++ tools
g++ -std=c++17 compress.cpp -o compress
g++ -std=c++17 decompress.cpp -o decompress

# 3) Create and activate Python virtualenv
python3 -m venv venv
source venv/bin/activate

# 4) Install Python deps
pip install Flask

# (Optional) Save for future:
echo "Flask>=2.2.5" > requirements.txt

# 5) Ensure upload/storage folders exist
mkdir -p uploads compressed decompressed

▶️ Running the App

# Activate your venv if not already
source venv/bin/activate

# Start Flask
export FLASK_APP=app.py
export FLASK_ENV=development     # enables debug mode
flask run

Then open your browser at http://127.0.0.1:5000/.
🗂️ Project Structure

.
├── app.py                   # Flask application
├── compress.cpp             # C++ compress tool
├── decompress.cpp           # C++ decompress tool
├── requirements.txt         # (optional) python deps
├── uploads/                 # temp store for uploaded files
├── compressed/              # stores .bin and .br results
├── decompressed/            # stores decompressed JSON
└── templates/
    ├── base.html
    ├── navbar.html
    ├── index.html
    ├── compress.html
    └── decompress.html

⚙️ Usage

    Compress

        Upload a .json file.

        The server runs ./compress → .bin, then brotli -f .bin → .bin.br.

        Download the .br and see the size & compression ratio.

    Decompress

        Upload a .br file.

        The server runs brotli -d -f → .bin, then ./decompress → _decompressed.json.

        Download the JSON.
