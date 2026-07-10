// Render the emulator surface SVGs to standalone files for visual review.
// Usage: node render_surfaces.mjs <outdir>
import { createServer } from 'vite'
import { createSSRApp, h } from 'vue'
import { renderToString } from 'vue/server-renderer'
import fs from 'fs'
import path from 'path'

const outdir = process.argv[2] || '/tmp'
const profiles = JSON.parse(fs.readFileSync(new URL('../bin/data/device_profiles.json', import.meta.url)))

const display = {
  page: 'All Surfaces',
  profile: '',
  virtual: true,
  labels: ['Quad-1', 'Mapping', 'Grp', 'noise', 'line', 'engine_speed', '', ''],
  values: [0.26, 0.96, 0.0, 0.62, 0.39, 0.25, 0, 0],
  cueGrid: { rows: 8, cols: 8, bank: 'Bank-1', cells: [
    { row: 6, col: 0, color: '#171717', isPlaying: false, name: 'Cue-1' },
    { row: 5, col: 1, color: '#6D261D', isPlaying: true,  name: 'Cue-2' },
  ] },
}

const targets = [
  ['push3',     '/src/components/mock/Push3Surface.vue',       'Push3'],
  ['fp16',      '/src/components/mock/Faderport16Surface.vue', 'Faderport16'],
  ['platformm', '/src/components/mock/PlatformMSurface.vue',   'PlatformM'],
]

const vite = await createServer({ server: { middlewareMode: true }, appType: 'custom', logLevel: 'error' })
for (const [name, modPath, profileName] of targets) {
  const mod = await vite.ssrLoadModule(modPath)
  const profile = profiles.find(p => p.name === profileName)
  const app = createSSRApp({
    render: () => h(mod.default, {
      components: profile?.components || [],
      display: { ...display, profile: profileName },
      mode: 'emulator',
      channels: [],
      bindings: {},
    }),
  })
  let svg = await renderToString(app)
  // strip SSR comment anchors that confuse standalone SVG parsing
  svg = svg.replace(/<!--[\s\S]*?-->/g, '')
  const file = path.join(outdir, `${name}.svg`)
  fs.writeFileSync(file, `<?xml version="1.0"?>\n${svg}`)
  console.log('wrote', file)
}
await vite.close()
