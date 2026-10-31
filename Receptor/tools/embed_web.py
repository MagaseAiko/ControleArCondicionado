# Script pré-build do PlatformIO: compacta ../docs/index.html (a mesma página
# publicada no GitHub Pages) e gera include/web_index_gz.h para o modo Wi-Fi.
Import("env")

import gzip
import os

project_dir = env["PROJECT_DIR"]
src = os.path.normpath(os.path.join(project_dir, "..", "docs", "index.html"))
dst = os.path.join(project_dir, "include", "web_index_gz.h")

PLACEHOLDER = b"<!DOCTYPE html><html><body><h1>docs/index.html nao encontrado</h1></body></html>"


def generate():
    if os.path.exists(src):
        if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
            return
        with open(src, "rb") as f:
            html = f.read()
    else:
        print("embed_web: %s nao encontrado, usando pagina provisoria" % src)
        html = PLACEHOLDER

    data = gzip.compress(html, compresslevel=9, mtime=0)
    lines = []
    for i in range(0, len(data), 20):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 20]) + ",")

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w") as f:
        f.write("// Gerado por tools/embed_web.py a partir de docs/index.html. Nao edite.\n")
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("static const uint8_t WEB_INDEX_GZ[] PROGMEM = {\n")
        f.write("\n".join(lines))
        f.write("\n};\nstatic const size_t WEB_INDEX_GZ_LEN = %d;\n" % len(data))
    print("embed_web: %s -> %d bytes (gzip)" % (os.path.basename(dst), len(data)))


generate()
