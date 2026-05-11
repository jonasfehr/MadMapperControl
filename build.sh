#!/bin/bash

# Build script for MadMapper Controller web UI and app integration

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
WEB_DIR="$SCRIPT_DIR/web"
APP_DIR="$SCRIPT_DIR"

echo "=========================================="
echo "MadMapper Controller Build Script"
echo "=========================================="

# Step 1: Install web dependencies
echo ""
echo "📦 Installing web dependencies..."
cd "$WEB_DIR"
npm install --legacy-peer-deps

# Step 2: Build web assets
echo ""
echo "🔨 Building web assets..."
npm run build

# Step 3: Verify bin/data/web was created
if [ ! -f "$APP_DIR/bin/data/web/index.html" ]; then
    echo "❌ Web build failed: index.html not found"
    exit 1
fi
echo "✓ Web assets built successfully"

# Step 4: Build C++ app
echo ""
echo "🔨 Building C++ app..."
cd "$APP_DIR"
make clean
make Debug -j -s

echo ""
echo "=========================================="
echo "✓ Build complete!"
echo "=========================================="
echo ""
echo "To run the app:"
echo "  make RunDebug"
echo ""
echo "Then open http://localhost:8080 in your browser"
echo "=========================================="
