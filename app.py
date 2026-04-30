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
ALLOWED_EXTENSIONS = {'json', 'cjdc'}

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

    base = fname.rsplit('.',1)[0]
    comp_name = f"{base}.cjdc"
    comp_path = os.path.join(app.config['COMPRESSED_FOLDER'], comp_name)
    result = subprocess.run(['./compress', in_path, comp_path], capture_output=True)
    if result.returncode != 0:
        flash('Compression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    orig_size = os.path.getsize(in_path)
    comp_size = os.path.getsize(comp_path)
    ratio = f"{comp_size}/{orig_size} ({comp_size/orig_size:.2%})"

    return render_template('compress.html', orig=fname, comp=comp_name,
                           orig_size=orig_size, comp_size=comp_size, ratio=ratio)

@app.route('/decompress', methods=['POST'])
def decompress_file():
    file = request.files.get('file')
    if not file or not allowed_file(file.filename, {'cjdc'}):
        flash('Please upload a valid CJDC file (.cjdc)')
        return redirect(url_for('index'))
    fname = secure_filename(file.filename)
    comp_path = os.path.join(app.config['COMPRESSED_FOLDER'], fname)
    file.save(comp_path)
    comp_size = os.path.getsize(comp_path)

    base = fname.rsplit('.',1)[0]
    decomp_name = f"{base}_decompressed.json"
    out_path = os.path.join(app.config['DECOMPRESSED_FOLDER'], decomp_name)
    result = subprocess.run(['./decompress', comp_path, out_path], capture_output=True)
    if result.returncode != 0:
        flash('Decompression failed: ' + result.stderr.decode())
        return redirect(url_for('index'))

    try:
        os.remove(comp_path)
    except OSError:
        pass

    decomp_size = os.path.getsize(out_path)

    return render_template('decompress.html', comp=fname, decomp=decomp_name,
                           comp_size=comp_size, decomp_size=decomp_size)

@app.route('/downloads/<folder>/<filename>')
def download_file(folder, filename):
    if folder not in ('compressed', 'decompressed'):
        flash('Invalid download folder')
        return redirect(url_for('index'))
    folder_path = app.config[f'{folder.upper()}_FOLDER']
    return send_from_directory(folder_path, filename, as_attachment=True)

if __name__ == '__main__':
    app.run(debug=True)
