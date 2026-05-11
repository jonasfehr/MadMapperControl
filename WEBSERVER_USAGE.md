# MadMapper Controller Web Server

## Overview
The application now includes an HTTP webserver for web-based GUI control of MadMapper configurations.

## Accessing the Web Interface

1. **Start the application:**
   ```bash
   cd /Users/jonasfehr/Documents/openFrameworks/apps/myApps/MadMapperControl_MM6_V2
   make RunDebug
   ```
   Or run the binary directly:
   ```bash
   ./bin/MadMapperControl_MM6_V2_debug.app/Contents/MacOS/MadMapperControl_MM6_V2_debug
   ```

2. **Open in web browser:**
   Navigate to: `http://localhost:8080`

3. **Features available:**
   - **Page Editor**: View and edit page configurations in JSON format
   - **Parameter Browser**: Search and browse OSCQuery parameters
   - **Status Display**: Shows current app status, active profile, and connected servers
   - **Configuration Management**: Save page changes and undo edits

## REST API Endpoints

- `GET /api/config` - Get current configuration and server status
- `GET /api/pages` - Get page definitions
- `POST /api/pages` - Update page definitions
- `GET /api/parameters` - Get OSCQuery parameter tree

## Technical Details

- **Server Port**: 8080
- **Technology**: Poco HTTP library (C++)
- **Frontend**: Vue 3 with Vite
- **Static Files**: Located in `bin/data/web/`

## Troubleshooting

If the webserver is not responding:
1. Ensure the application is running
2. Check that port 8080 is not in use: `lsof -i :8080`
3. Verify the app logs for "WebServer started successfully"
4. Try refreshing the page in your browser

