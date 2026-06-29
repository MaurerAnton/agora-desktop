#!/bin/bash
# Qt6 build for Agora Linux (aarch64, PineTab 2 / PinePhone)
set -e
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
OBJ="$PROJ/build/obj"
BIN="$PROJ/build/agora"
MOC=/usr/lib/qt6/moc

CXX_FLAGS="-std=c++20 -O2 -I $PROJ/src -I $PROJ/src/thirdparty/json \
  -I /usr/include/qt6 -I /usr/include/qt6/QtWidgets \
  -I /usr/include/qt6/QtGui -I /usr/include/qt6/QtCore \
  -I /usr/include/alsa -I /usr/include/pipewire-0.3 -I /usr/include/spa-0.2"

LIBS="-lQt6Widgets -lQt6Gui -lQt6Core \
  -lsqlite3 -lcurl -lssl -lcrypto -lglib-2.0 \
  -lasound -lpipewire-0.3 -lportaudio -lpthread"

SOURCES=(
    src/main.cpp
    src/db/database.cpp
    src/models/message.cpp
    src/models/conversation.cpp
    src/api/http_client.cpp
    src/api/provider.cpp
    src/audio/recorder.cpp
    src/utils/config.cpp
    src/ui/main_window_qt.cpp
    src/ui/theme.cpp
)

mkdir -p "$OBJ"
rm -f "$OBJ"/*.o

echo "=== MOC ==="
$MOC -I "$PROJ/src" -I /usr/include/qt6 \
  -I /usr/include/qt6/QtWidgets -I /usr/include/qt6/QtGui -I /usr/include/qt6/QtCore \
  "$PROJ/src/ui/main_window_qt.hpp" -o "$OBJ/moc_main_window_qt.cpp"
g++ $CXX_FLAGS -c "$OBJ/moc_main_window_qt.cpp" -o "$OBJ/moc_main_window_qt.o"

echo "=== COMPILE ==="
for src in "${SOURCES[@]}"; do
    echo "  $src"
    g++ $CXX_FLAGS -c "$PROJ/$src" -o "$OBJ/$(basename "$src" .cpp).o"
done

echo "=== LINK ==="
g++ -std=c++20 "$OBJ"/*.o $LIBS -o "$BIN"

echo "=== DONE ==="
ls -lh "$BIN"
file "$BIN"
