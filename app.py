# File: app.py
from flask import Flask, render_template, request, send_from_directory, flash, redirect, url_for
from werkzeug.utils import secure_filename
import os
import subprocess

app = Flask(__name__)
app.config['SECRET_KEY'] = 'MY SECRET KEY'
app.config['UPLOAD_FOLDER'] = 'uploads'
app.config['COMPRESSED_FOLDER'] = 'compressed'
app.config['DECOMPRESSED_FOLDER'] = 'decompressed'
ALLOWED_EXTENSIONS = {'json', 'bin', 'br'}

# Ensure folders exist
for folder in (app.config['UPLOAD_FOLDER'], app.config['COMPRESSED_FOLDER'], app.config['DECOMPRESSED_FOLDER']):
    os.makedirs(folder, exist_ok=True)

# Helper
def allowed_file(filename, exts):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in exts

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/compress', methods=['POST'])
def compress_file():
    file = request.files.get('file')
    if not file or not allowed_file(file.filename, {'json'}):
        flash('Please upload a valid JSON file')
        return redirect(url_for('index'))
    fname = secure_filename(file.filename)
    in_path = os.path.join(app.config['UPLOAD_FOLDER'], fname)
    file.save(in_path)

    # 1) run C++ compressor -> .bin in COMPRESSED_FOLDER
    base = fname.rsplit('.',1)[0]
    bin_name = f"{base}.bin"
    bin_path = os.path.join(app.config['COMPRESSED_FOLDER'], bin_name)
    result = subprocess.run(['./compress', in_path, bin_path], capture_output=True)
    if result.returncode != 0:
        flash('Compression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    # 2) run Brotli on the binary -> .br in COMPRESSED_FOLDER
    br_name = f"{bin_name}.br"
    br_path = os.path.join(app.config['COMPRESSED_FOLDER'], br_name)
    # use -o to specify output, avoid default overwrite behavior
    result = subprocess.run(['brotli', '-f', '-o', br_path, bin_path], capture_output=True)
    if result.returncode != 0:
        flash('Brotli compression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    # Remove intermediate .bin
    try:
        os.remove(bin_path)
    except OSError:
        pass

    # Compute sizes
    orig_size = os.path.getsize(in_path)
    br_size = os.path.getsize(br_path)
    ratio = f"{br_size}/{orig_size} ({br_size/orig_size:.2%})"

    return render_template('compress.html', orig=fname, comp=br_name,
                           orig_size=orig_size, comp_size=br_size, ratio=ratio)

@app.route('/decompress', methods=['POST'])
def decompress_file():
    file = request.files.get('file')
    if not file or not allowed_file(file.filename, {'br'}):
        flash('Please upload a valid Brotli-compressed file (.br)')
        return redirect(url_for('index'))
    fname = secure_filename(file.filename)
    # Save to COMPRESSED_FOLDER to handle decompression
    br_path = os.path.join(app.config['COMPRESSED_FOLDER'], fname)
    file.save(br_path)

    # 1) Brotli decompress -> .bin in DECOMPRESSED_FOLDER intermediate
    base = fname.rsplit('.',1)[0]
    bin_name = f"{base}.bin"
    bin_path = os.path.join(app.config['DECOMPRESSED_FOLDER'], bin_name)
    result = subprocess.run(['brotli', '-d', '-f', '-o', bin_path, br_path], capture_output=True)
    if result.returncode != 0:
        flash('Brotli decompression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    # 2) run C++ decompress -> JSON in DECOMPRESSED_FOLDER
    decomp_name = f"{base}_decompressed.json"
    out_path = os.path.join(app.config['DECOMPRESSED_FOLDER'], decomp_name)
    result = subprocess.run(['./decompress', bin_path, out_path], capture_output=True)
    if result.returncode != 0:
        flash('Decompression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    # Cleanup intermediates
    try:
        os.remove(br_path)
        os.remove(bin_path)
    except OSError:
        pass

    # Compute sizes
    br_size = os.path.getsize(br_path) if os.path.exists(br_path) else 0
    decomp_size = os.path.getsize(out_path)

    return render_template('decompress.html', comp=fname, decomp=decomp_name,
                           comp_size=br_size, decomp_size=decomp_size)

@app.route('/downloads/<folder>/<filename>')
def download_file(folder, filename):
    if folder not in ('compressed', 'decompressed'):
        flash('Invalid download folder')
        return redirect(url_for('index'))
    folder_path = app.config[f'{folder.upper()}_FOLDER']
    return send_from_directory(folder_path, filename, as_attachment=True)

if __name__ == '__main__':
    app.run(debug=True)
