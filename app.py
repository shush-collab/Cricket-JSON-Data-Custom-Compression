from flask import Flask, render_template, redirect, url_for
app = Flask(__name__)
app.config['SECRET_KEY'] = "MY SECRET KEY"


@app.route('/')
def index():
    return render_template("index.html")

@app.errorhandler(404)
def page_not_found(e):
    return render_template("404.html"), 404

@app.errorhandler(500)
def internal_error(e):
    return render_template("500.html"), 500

@app.route('/compression')
def comp():
    return render_template("comp.html")

@app.route('/benchmark')
def bench():
    return render_template("bench.html")

if __name__ == "__main__":
    app.run(debug=True)