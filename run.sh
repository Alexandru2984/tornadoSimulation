#!/bin/bash
# Script pentru build & run proiect tornada
# Usage:
#   ./run.sh [build|run|clean|rebuild|help]

BUILD_DIR="build"
BINARY="$BUILD_DIR/tornado"

function build() {
    if [ ! -d "$BUILD_DIR" ]; then
        mkdir "$BUILD_DIR"
    fi
    cd "$BUILD_DIR" || exit 1
    cmake .. && make
    cd ..
}

function run() {
    if [ ! -f "$BINARY" ]; then
        echo "[INFO] Binarul nu exista. Se face build..."
        build || { echo "[EROARE] Build esuat."; exit 1; }
    fi
    "$BINARY"
}

function clean() {
    echo "[INFO] Se sterge directorul de build..."
    rm -rf "$BUILD_DIR"
}

function help() {
    echo "Script de build & run pentru simulare tornada."
    echo "Optiuni:"
    echo "  build    - Compileaza proiectul"
    echo "  run      - Ruleaza binarul (face build daca e nevoie)"
    echo "  clean    - Sterge build-ul"
    echo "  rebuild  - Curata si compileaza din nou"
    echo "  help     - Afiseaza acest mesaj"
}

case "$1" in
    build)
        build
        ;;
    run|"")
        run
        ;;
    clean)
        clean
        ;;
    rebuild)
        clean && build
        ;;
    help)
        help
        ;;
    *)
        echo "[EROARE] Optiune necunoscuta: $1"
        help
        exit 1
        ;;
esac
