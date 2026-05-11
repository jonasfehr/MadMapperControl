# MadMapper Controller Web UI

Vue 3 + Vite web interface for editing MadMapper controller configuration and pages.

## Setup

### Prerequisites
- Node.js 16+ and npm

### Installation

```bash
# Install dependencies
npm install
```

## Development

```bash
# Start development server (http://localhost:5173)
# Proxies API calls to C++ app on localhost:8080
npm run dev
```

## Build

```bash
# Build for production
# Output goes to ../bin/data/web/
npm run build
```

The C++ app will serve these built assets at `http://localhost:8080`.

## Features

- **Page Editor**: Edit pages and subpages as JSON
- **Parameter Browser**: Searchable tree of all available parameters from MadMapper and secondary servers
- **Auto-Save**: Changes are automatically saved to `custom_page.json`
- **Multi-Tab Interface**: Switch between Pages and Subpages editing

## API Endpoints

Used by the web UI:

- `GET /api/pages` - Fetch current pages configuration
- `POST /api/pages` - Save updated pages configuration
- `GET /api/parameters` - Fetch OSCQuery parameter trees from all servers
- `GET /api/config` - Fetch current app configuration and status

## Architecture

The web UI communicates with the C++ openFrameworks app via REST API. The C++ app:
1. Runs an HTTP server on port 8080
2. Exposes REST API endpoints for configuration management
3. Serves static files from `bin/data/web/`

## Future Enhancements

- Real-time parameter control (sliders)
- Node-based controller mapping editor
- Undo/history functionality
- Parameter value monitoring
