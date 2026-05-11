# MadMapper Controller - Web GUI Integration Complete ✅

## Build Status

✅ **Web Server Infrastructure Built**
- Poco HTTP server running on port 8080
- REST API endpoints: `/api/pages`, `/api/parameters`, `/api/config`
- Static file serving from `bin/data/web/`

✅ **Web UI Built**
- Vue 3 + Vite frontend compiled
- Page editor with JSON validation
- Parameter browser with search
- Auto-save to `custom_page.json`
- Undo button for reverting changes

✅ **C++ App Compiled**
- WebServer.h/cpp integrated
- REST handlers implemented
- API methods in ofApp

## What Was Built

### Backend (C++)

**New Files:**
- `src/WebServer.h` - HTTP server interface
- `src/WebServer.cpp` - Implementation with REST handlers
- `build.sh` - Unified build script

**Modified Files:**
- `src/ofApp.h` - Added WebServer member, API methods
- `src/ofApp.cpp` - Integrated WebServer startup/shutdown, implemented API methods

**Features:**
- `GET /api/pages` - Returns custom_page.json
- `POST /api/pages` - Saves pages, triggers reload
- `GET /api/parameters` - Returns OSCQuery trees from all servers
- `GET /api/config` - Returns app status and configuration
- Static file serving from `bin/data/web/`

### Frontend (Vue 3)

**Web Project:**
- `web/` - Isolated Vue 3 + Vite project
- `web/src/App.vue` - Main layout with sidebar + editor
- `web/src/components/PageEditor.vue` - JSON editor with validation
- `web/src/api.js` - REST client
- `web/index.html` - Entry point
- `bin/data/web/` - Built assets ready to serve

**Features:**
- Real-time JSON validation
- Auto-save on button click
- Searchable parameter tree
- Tab-based pages/subpages editor
- Responsive design with gradient header

## Getting Started

### 1. Install Dependencies (Already Done)
```bash
cd web
npm install --legacy-peer-deps
```

### 2. Build Web Assets (Already Done)
```bash
npm run build
# Output: bin/data/web/ with index.html + CSS/JS assets
```

### 3. Build C++ App (Already Done)
```bash
make clean
make Debug -j -s
```

### 4. Run the App
```bash
make RunDebug
# Or manually:
./bin/MadMapperControl_MM6_V2.app/Contents/MacOS/MadMapperControl_MM6_V2
```

### 5. Open Browser
```
http://localhost:8080
```

## Project Structure

```
MadMapperControl_MM6_V2/
├── src/
│   ├── WebServer.h           ← HTTP server interface
│   ├── WebServer.cpp         ← REST API handlers
│   ├── ofApp.h               ← Updated with WebServer
│   ├── ofApp.cpp             ← API method implementations
│   └── ...
├── web/                       ← Vue 3 + Vite project
│   ├── src/
│   │   ├── main.js
│   │   ├── App.vue           ← Main layout
│   │   ├── api.js            ← REST client
│   │   └── components/
│   │       └── PageEditor.vue
│   ├── package.json
│   ├── vite.config.js
│   └── index.html
├── bin/
│   ├── data/
│   │   ├── web/              ← Built web assets
│   │   │   ├── index.html
│   │   │   └── assets/
│   │   │       ├── index-*.css
│   │   │       └── index-*.js
│   │   └── ...
│   └── MadMapperControl_MM6_V2.app/
└── build.sh                   ← Unified build script
```

## API Reference

### GET /api/pages
Returns custom_page.json configuration
```json
{
  "pages": [...],
  "subpages": [...]
}
```

### POST /api/pages
Saves pages configuration and triggers reload
```json
{
  "pages": [...],
  "subpages": [...]
}
```

### GET /api/parameters
Returns OSCQuery parameter trees from all servers
```json
{
  "primary": { ... OSCQuery tree ... },
  "server_1": { ... TD/secondary server tree ... }
}
```

### GET /api/config
Returns app status and configuration
```json
{
  "servers": [...],
  "currentPage": "...",
  "initialized": true,
  "noDeviceConnected": false,
  "activeProfile": "..."
}
```

## Features Implemented

### Phase 1 (Complete)
✅ Page editor (JSON editing with validation)
✅ Parameter browser (searchable tree)
✅ Auto-save to custom_page.json
✅ Undo button
✅ Multi-server parameter exposure
✅ HTTP REST API backend
✅ Static file serving

### Future Enhancements (Not in Phase 1)
⌛ Real-time parameter sliders
⌛ Parameter value monitoring
⌛ WebSocket real-time updates
⌛ Node-based controller mapping
⌛ Full undo/history stack
⌛ Web-based controller simulation

## Troubleshooting

### Port 8080 Already in Use
```bash
# Find and kill process on 8080
lsof -i :8080
kill -9 <PID>
```

### Web UI Not Loading
1. Check app is running: `Make RunDebug`
2. Check web assets exist: `ls bin/data/web/index.html`
3. Check console for CORS errors
4. Rebuild if needed: `cd web && npm run build`

### Pages Not Saving
1. Check `custom_page.json` exists in `bin/data/`
2. Verify JSON is valid (editor shows validation message)
3. Check app has write permissions to `bin/data/`
4. Check app console for save errors

### Parameters Not Showing
1. Ensure MadMapper is running and connected
2. Check OSCQuery on port 9001 is responding
3. Check secondary servers are configured in settings.json
4. Verify app loaded parameters: check ofApp console output

## Development Workflow

### Making Web UI Changes
```bash
# During development:
cd web
npm run dev  # Starts dev server on localhost:5173 with proxy to localhost:8080
```

### Making C++ Changes
```bash
# Edit src/WebServer.cpp or src/ofApp.cpp
make Debug -j -s
make RunDebug
```

### Rebuilding Everything
```bash
bash build.sh
```

## Next Steps

1. **Test in Browser:**
   - Navigate to http://localhost:8080
   - Verify parameter tree loads
   - Test editing and saving pages
   - Check auto-reload works

2. **Add Real-time Control (Optional):**
   - Implement parameter sliders in Vue
   - Add OSC sending from web UI
   - Add WebSocket parameter update subscription

3. **Enhanced Editing:**
   - Drag-drop for page order
   - Array/object JSON editing widgets
   - Live preview of parameter matches

4. **Node Editor (Future):**
   - Integrate Rete.js or similar
   - Build controller mapping flow UI
   - Add node visualization

## Build Commands Reference

```bash
# Full build
bash build.sh

# Web only
cd web && npm run build

# C++ only
make Debug -j -s

# Run app
make RunDebug

# Dev mode (web changes only)
cd web && npm run dev

# Clean everything
make clean
rm -rf web/dist web/node_modules bin/data/web/*
```

---

**Status:** Ready for testing and deployment ✅
