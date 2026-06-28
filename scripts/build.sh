#!/bin/bash
# Build script for Agora Linux on PineTab 2 / PinePhone (aarch64)
# Direct g++ build — no cmake required

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OBJ_DIR="${PROJECT_DIR}/build/obj"
BIN="${PROJECT_DIR}/build/agora"

echo "=== agora-desktop v0.1.0 Build ==="

mkdir -p "$OBJ_DIR"

# GTK4 compile flags (find them automatically or use hardcoded)
if pkg-config --exists gtk4 2>/dev/null; then
    GTK_CFLAGS=$(pkg-config --cflags gtk4)
    GTK_LIBS=$(pkg-config --libs gtk4)
else
    GTK_CFLAGS="-I/usr/include/gtk-4.0 -I/usr/include/pango-1.0 -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/cairo -I/usr/include/graphene-1.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/harfbuzz -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/fribidi -I/usr/lib/graphene-1.0/include"
    GTK_LIBS="-lgtk-4 -lglib-2.0 -lgobject-2.0 -lgio-2.0"
fi

CXX_FLAGS="-std=c++20 -O2 -I ${PROJECT_DIR}/src -I ${PROJECT_DIR}/src/thirdparty/json ${GTK_CFLAGS}"
LIBS="${GTK_LIBS} -lsqlite3 -lcurl -lssl -lcrypto -lpthread"

# Source files in order
SOURCES=(
    "src/main.cpp"
    "src/db/database.cpp"
    "src/models/message.cpp"
    "src/models/conversation.cpp"
    "src/api/http_client.cpp"
    "src/api/provider.cpp"
    "src/audio/recorder.cpp"
    "src/utils/config.cpp"
    "src/ui/main_window_gtk.cpp"
)

echo "Compiling ${#SOURCES[@]} source files..."

for src in "${SOURCES[@]}"; do
    obj="${OBJ_DIR}/$(basename "${src}" .cpp).o"
    echo "  ${src}"
    g++ ${CXX_FLAGS} -c "${PROJECT_DIR}/${src}" -o "$obj"
done

echo "Linking..."
g++ -std=c++20 ${OBJ_DIR}/*.o ${LIBS} -o "$BIN"

echo ""
echo "=== Build Complete ==="
echo "Binary: ${BIN}"
ls -lh "$BIN"
file "$BIN"
echo ""
echo "Run: ${BIN}"
echo "Config: ~/.config/agora.json"
