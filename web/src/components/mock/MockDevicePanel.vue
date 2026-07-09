<script setup>
import { ref, shallowRef, watch, computed, toRef, onMounted, onUnmounted } from 'vue'
import Push3Surface      from './Push3Surface.vue'
import Faderport16Surface from './Faderport16Surface.vue'
import PlatformMSurface   from './PlatformMSurface.vue'
import { useDeviceMappings } from '../../composables/useDeviceMappings.js'
import { apiClient } from '../../api.js'

const props = defineProps({
  pages:         { type: Object,  default: () => ({ pages: [], subpages: [] }) },
  config:        { type: Object,  default: () => null },
  allParameters: { type: Array,   default: () => [] },
})

// Each emulated device maps to a profile in device_profiles.json by name —
// control-button naming differs per device, so the surface reads its own profile.
const DEVICES = [
  { id: 'push3',     label: 'Ableton Push 3', component: Push3Surface,       maxCh: 64, profile: 'Push3' },
  { id: 'fp16',      label: 'Faderport 16',    component: Faderport16Surface, maxCh: 14, profile: 'Faderport16' },
  { id: 'platformm', label: 'Platform M+',     component: PlatformMSurface,   maxCh: 8,  profile: 'PlatformM' },
]

const selectedId  = ref('push3')
const currentDev  = shallowRef(DEVICES[0])
const midiLog     = ref([])
const bankOffset  = ref(0)

watch(selectedId, id => {
  currentDev.value = DEVICES.find(d => d.id === id) || DEVICES[0]
  bankOffset.value = 0
})

// ── channel mapping ───────────────────────────────────────────────
const { resolvedChannels, currentPageName } = useDeviceMappings(
  toRef(props, 'pages'),
  toRef(props, 'config'),
  toRef(props, 'allParameters')
)

// Reset bank to 0 whenever the active page changes
watch(currentPageName, () => { bankOffset.value = 0 })

const deviceChannels = computed(() => {
  const maxCh = currentDev.value.maxCh
  const start = bankOffset.value * maxCh
  return resolvedChannels.value.slice(start, start + maxCh)
})

function onBankChange(delta) {
  const maxCh    = currentDev.value.maxCh
  const maxBanks = Math.max(0, Math.ceil(resolvedChannels.value.length / maxCh) - 1)
  bankOffset.value = Math.max(0, Math.min(maxBanks, bankOffset.value + delta))
}

// ── Per-device profiles (each surface reads its own mapping) ───────
const allProfiles  = ref([])
const displayState = ref(null)
let displayPoll = null

// The profile for the currently-selected surface (not necessarily the backend's).
const surfaceProfile = computed(() =>
  allProfiles.value.find(p => p.name === currentDev.value.profile) || null
)
const profileComponents = computed(() => surfaceProfile.value?.components || [])

// Map components' roles to the bindings shape the surfaces use for tint/tooltips
const bindingsMap = computed(() => {
  const m = {}
  for (const c of profileComponents.value) {
    if (!c.role) continue
    const t = c.type === 'note_toggle' ? 'note'
            : (c.type === 'encoder_relative' || c.type === 'encoder' || c.type === 'cc_toggle') ? 'cc'
            : c.type
    m[`${t}:${c.channel}:${c.address}`] = { role: c.role, label: c.label }
  }
  return m
})

async function fetchProfiles() {
  try { allProfiles.value = await apiClient.fetchProfiles() } catch (_) {}
}

// Device switching is only meaningful in virtual mode; connected hardware is
// authoritative and the emulator mirrors it.
const canSwitch = computed(() => displayState.value?.virtual !== false)
const userPickedDevice = ref(false)

async function pollDisplay() {
  try {
    const d = await apiClient.fetchDisplay()
    displayState.value = d
    const name = d?.profile || ''
    const want = DEVICES.find(dev => dev.profile === name)?.id
    // Hardware wins; in virtual mode respect a manual pick.
    if (want && want !== selectedId.value && (d.virtual === false || !userPickedDevice.value)) {
      selectedId.value = want
    }
  } catch (_) {}
}

function pickDevice(id) {
  if (!canSwitch.value) return
  userPickedDevice.value = true
  selectedId.value = id
  const dev = DEVICES.find(d => d.id === id)
  if (dev) apiClient.setEmulatorSurface(dev.profile).catch(() => {})
}

// ── MIDI learn bridge ─────────────────────────────────────────────
const learnActive  = ref(false)
const learnCapture = ref(null)
let learnPoll = null

async function pollLearnStatus() {
  try {
    const s = await apiClient.learnStatus()
    learnActive.value = s.active
    if (s.ready && !learnCapture.value) learnCapture.value = s
    if (s.ready) learnCapture.value = s
  } catch (_) {}
}

onMounted(() => {
  learnPoll = setInterval(pollLearnStatus, 400)
  displayPoll = setInterval(pollDisplay, 300)
  fetchProfiles()
  pollDisplay()
})
onUnmounted(() => { clearInterval(learnPoll); clearInterval(displayPoll) })

// ── MIDI input from the emulated surface ──────────────────────────
// Injected into the app's real MIDI pipeline — the emulator substitutes the controller.
const STATUS_NOTE_ON = 144
const STATUS_CC      = 176
const STATUS_PITCH   = 224

function onMidiInput(ev) {
  if (ev.type === '__learn_click__') return
  const status = ev.type === 'note' ? STATUS_NOTE_ON : ev.type === 'pitch_bend' ? STATUS_PITCH : STATUS_CC
  const payload = {
    channel: ev.channel,
    status,
    control: status === STATUS_CC ? ev.address : 0,
    pitch:   status === STATUS_NOTE_ON ? ev.address : 0,
    value:   ev.value,
  }
  const kind = status === STATUS_NOTE_ON ? 'Note' : status === STATUS_PITCH ? 'Pitch' : 'CC'
  log(`${kind.padEnd(5)} ch:${ev.channel}  addr:${ev.address}  val:${ev.value}${ev.name ? '  (' + ev.name + ')' : ''}`)
  apiClient.injectMidi(payload).catch(() => {})
  if (learnActive.value) apiClient.learnInject(payload).catch(() => {})
}

const bankLabel = computed(() => {
  const maxCh  = currentDev.value.maxCh
  const total  = resolvedChannels.value.length
  const banks  = Math.max(1, Math.ceil(total / maxCh))
  return `Bank ${bankOffset.value + 1}/${banks}`
})

// ── Mapped Controls ────────────────────────────────────────────────
// A guaranteed-complete list of every role-bound component in the active
// profile. The hand-drawn surfaces are device-specific and may not expose
// (or correctly wire) every mapped control — this list always can.
function midiTypeOf(c) {
  if (c.type === 'note' || c.type === 'note_toggle') return 'note'
  if (c.type === 'pitch_bend') return 'pitch_bend'
  return 'cc' // cc, cc_toggle, encoder, encoder_relative
}
function kindOf(c) {
  if (c.type === 'encoder_relative') return 'relative'
  if (c.type === 'pitch_bend') return 'fader'
  if (c.type === 'encoder') return 'knob'
  return 'button'
}
function shortRole(r) { return r.replace(/^(nav|action|fixed|param)\./, '') }

const controlGroups = computed(() => {
  const groups = { Navigation: [], Parameters: [], Fixed: [], Other: [] }
  for (const c of profileComponents.value) {
    if (!c.role) continue
    const item = { ...c, kind: kindOf(c), midiType: midiTypeOf(c) }
    const r = c.role
    if (r.startsWith('nav.') || r.startsWith('action.')) groups.Navigation.push(item)
    else if (r.startsWith('param.')) groups.Parameters.push(item)
    else if (r.startsWith('fixed.')) groups.Fixed.push(item)
    else groups.Other.push(item)
  }
  groups.Parameters.sort((a, b) => {
    const na = parseInt(a.role.split('.')[1]) || 0
    const nb = parseInt(b.role.split('.')[1]) || 0
    return na - nb || a.role.localeCompare(b.role)
  })
  return Object.entries(groups).filter(([, list]) => list.length)
})
const mappedCount = computed(() => profileComponents.value.filter(c => c.role).length)

// Momentary press → release so buttons re-trigger every click (a stuck value
// never fires the change listener again on the C++ side).
function triggerButton(c) {
  onMidiInput({ type: c.midiType, channel: c.channel, address: c.address, value: 127, name: c.label })
  setTimeout(() => onMidiInput({ type: c.midiType, channel: c.channel, address: c.address, value: 0, name: c.label }), 120)
}
// Relative encoder: 7-bit two's-complement tick (+1 = 1, −1 = 127).
function nudge(c, dir) {
  onMidiInput({ type: 'cc', channel: c.channel, address: c.address, value: dir > 0 ? 1 : 127, name: c.label })
}
function setControl(c, e) {
  const norm = Number(e.target.value) / 100
  if (c.midiType === 'pitch_bend')
    onMidiInput({ type: 'pitch_bend', channel: c.channel, address: 0, value: Math.round(norm * 16383), name: c.label })
  else
    onMidiInput({ type: 'cc', channel: c.channel, address: c.address, value: Math.round(norm * 127), name: c.label })
}

function log(msg) {
  midiLog.value.unshift({ t: new Date().toISOString().slice(11, 23), msg })
  if (midiLog.value.length > 80) midiLog.value.length = 80
}
function clearLog() { midiLog.value = [] }
</script>

<template>
  <div class="mock-panel">

    <!-- header -->
    <div class="mock-header">
      <div class="mock-title-group">
        <span class="mock-title">Device Emulator</span>
        <span v-if="displayState?.page || currentPageName" class="page-badge">
          <span class="page-dot"></span>{{ displayState?.page || currentPageName }}
        </span>
        <span v-if="resolvedChannels.length" class="ch-badge">
          {{ resolvedChannels.length }} ch mapped
        </span>
        <span v-if="resolvedChannels.length > currentDev.maxCh" class="bank-badge">
          {{ bankLabel }}
        </span>
      </div>
      <div class="device-select">
        <span v-if="displayState?.virtual" class="virtual-badge">VIRTUAL</span>
        <button
          v-for="d in DEVICES" :key="d.id"
          class="dev-btn"
          :class="{ active: selectedId === d.id }"
          :disabled="!canSwitch && selectedId !== d.id"
          :title="!canSwitch ? 'Hardware connected — emulator mirrors the active device' : d.label"
          @click="pickDevice(d.id)"
        >{{ d.label }}</button>
      </div>
    </div>

    <!-- Learn banner -->
    <div v-if="learnActive || learnCapture" class="learn-banner" :class="{ captured: learnCapture }">
      <template v-if="learnCapture">
        ✓ Captured: {{ learnCapture.type?.toUpperCase() }}
        {{ learnCapture.type === 'cc' ? 'CC ' + learnCapture.control : learnCapture.type === 'note' ? 'Note ' + learnCapture.pitch : 'Pitch Bend' }}
        ch {{ learnCapture.channel }}
        — go to <strong>Mapping</strong> tab to assign
        <button class="learn-dismiss" @click="learnCapture = null">✕</button>
      </template>
      <template v-else>
        <span class="learn-pulse">●</span> MIDI Learn active — interact with a control below
      </template>
    </div>

    <!-- surface -->
    <div class="surface-wrap">
      <component
        :is="currentDev.component"
        :channels="deviceChannels"
        :components="profileComponents"
        :bindings="bindingsMap"
        :display="displayState"
        mode="emulator"
        @midi-input="onMidiInput"
        @bank-change="onBankChange"
      />
    </div>

    <!-- Mapped Controls — guaranteed-complete trigger list -->
    <div v-if="mappedCount" class="mapped-controls">
      <div class="mc-head">
        <span class="mc-title">Mapped Controls</span>
        <span class="mc-sub">{{ mappedCount }} bound — every mapped feature, guaranteed reachable</span>
      </div>
      <div class="mc-groups">
        <div v-for="[group, list] in controlGroups" :key="group" class="mc-group">
          <div class="mc-group-label">{{ group }}</div>
          <div class="mc-items">
            <template v-for="c in list" :key="c.label">
              <button
                v-if="c.kind === 'button'"
                class="mc-btn"
                @mousedown="triggerButton(c)"
                :title="c.label + '  →  ' + c.role"
              >
                <span class="mc-role">{{ shortRole(c.role) }}</span>
                <span class="mc-lbl">{{ c.label }}</span>
              </button>
              <div v-else-if="c.kind === 'relative'" class="mc-enc" :title="c.label + '  →  ' + c.role">
                <button class="mc-nudge" @click="nudge(c, -1)">−</button>
                <span class="mc-role">{{ shortRole(c.role) }}</span>
                <button class="mc-nudge" @click="nudge(c, 1)">+</button>
              </div>
              <div v-else class="mc-fader" :title="c.label + '  →  ' + c.role">
                <span class="mc-role">{{ shortRole(c.role) }}</span>
                <input type="range" min="0" max="100" value="0" @input="setControl(c, $event)" />
              </div>
            </template>
          </div>
        </div>
      </div>
    </div>

    <!-- MIDI log -->
    <div class="midi-log">
      <div class="log-header">
        <span class="log-title">MIDI Monitor</span>
        <span v-if="midiLog.length" class="log-count">{{ midiLog.length }}</span>
        <button class="clear-btn" @click="clearLog">Clear</button>
      </div>
      <div class="log-entries">
        <div v-for="(entry, i) in midiLog" :key="i" class="log-entry">
          <span class="log-time">{{ entry.t }}</span>
          <span class="log-msg">{{ entry.msg }}</span>
        </div>
        <div v-if="!midiLog.length" class="log-empty">— interact with a control to see events —</div>
      </div>
    </div>

  </div>
</template>

<style scoped>
.mock-panel {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 14px;
  background: var(--bg-shell, #111);
  min-height: 100%;
  overflow: auto;
}

/* ── Learn banner ── */
.learn-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 14px;
  border-radius: 5px;
  font-size: 12px;
  font-weight: 600;
  background: #1a2a1a;
  border: 1px solid #3a6b3a;
  color: #80c880;
}
.learn-banner.captured {
  background: #1a2a30;
  border-color: #2d5a7a;
  color: #80d8ff;
}
.learn-pulse {
  color: #4caf50;
  animation: pulse 1s infinite;
}
.learn-dismiss {
  margin-left: auto;
  background: none;
  border: none;
  color: inherit;
  cursor: pointer;
  font-size: 13px;
  opacity: 0.6;
}
.learn-dismiss:hover { opacity: 1; }
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0.3; }
}

/* ── header ─────────────────────────────────────────────────────── */
.mock-header {
  display: flex;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}
.mock-title-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
}
.mock-title {
  font-size: 10px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-dim, #555);
}
.page-badge {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 2px 7px;
  border: 1px solid var(--accent-dim, #1a4a50);
  border-radius: var(--radius-xs, 2px);
  font-size: 10px;
  color: var(--accent, #18c8da);
  background: var(--bg-active, #0e2a30);
}
.page-dot {
  width: 5px; height: 5px;
  border-radius: 50%;
  background: var(--accent, #18c8da);
  animation: pulse 2s infinite;
}
@keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }
.ch-badge {
  font-size: 9px;
  color: var(--text-dim, #555);
  padding: 2px 7px;
  border: 1px solid var(--border-strong, #2a2a2c);
  border-radius: var(--radius-xs, 2px);
}
.bank-badge {
  font-size: 9px;
  color: var(--text-muted, #888);
  padding: 2px 7px;
  border: 1px solid var(--border-strong, #2a2a2c);
  border-radius: var(--radius-xs, 2px);
  font-variant-numeric: tabular-nums;
}
.device-select { display: flex; gap: 4px; align-items: center; }
.virtual-badge {
  font-size: 9px;
  font-weight: 700;
  letter-spacing: 0.08em;
  color: #e0a030;
  border: 1px solid #7a5a20;
  border-radius: var(--radius-xs, 2px);
  padding: 2px 7px;
  background: #2a2010;
}
.dev-btn {
  height: 26px;
  padding: 0 10px;
  border-radius: var(--radius-xs, 2px);
  border: 1px solid var(--border-strong, #333);
  background: var(--bg-panel, #1e1e1e);
  color: var(--text-dim, #666);
  font-size: 11px;
  cursor: pointer;
  transition: background 60ms, color 60ms;
}
.dev-btn:hover  { background: var(--bg-panel-soft, #252525); color: var(--text-base, #aaa); }
.dev-btn:disabled { opacity: 0.35; cursor: not-allowed; }
.dev-btn.active {
  background: var(--bg-active, #1a3035);
  border-color: var(--accent, #18c8da);
  color: var(--accent, #18c8da);
}

/* ── surface ─────────────────────────────────────────────────────── */
.surface-wrap { overflow-x: auto; padding-bottom: 4px; }

/* ── mapped controls ─────────────────────────────────────────────── */
.mapped-controls {
  background: var(--bg-panel, #1e1e1e);
  border: 1px solid var(--border-strong, #333);
  border-radius: var(--radius-xs, 2px);
  overflow: hidden;
  flex-shrink: 0;
}
.mc-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  padding: 5px 10px;
  background: var(--bg-shell, #111);
  border-bottom: 1px solid var(--border-strong, #333);
}
.mc-title {
  font-size: 9px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.07em;
  color: var(--text-dim, #555);
}
.mc-sub { font-size: 9px; color: var(--text-dim, #444); }
.mc-groups { display: flex; flex-direction: column; gap: 2px; padding: 8px; }
.mc-group { display: flex; gap: 8px; align-items: flex-start; }
.mc-group-label {
  flex: 0 0 74px;
  font-size: 9px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--text-dim, #555);
  padding-top: 6px;
}
.mc-items { display: flex; flex-wrap: wrap; gap: 4px; flex: 1; }
.mc-btn {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 1px;
  min-width: 62px;
  padding: 4px 7px;
  border-radius: var(--radius-xs, 2px);
  border: 1px solid var(--border-strong, #333);
  background: var(--bg-panel-soft, #252525);
  cursor: pointer;
  transition: background 60ms, border-color 60ms;
}
.mc-btn:hover { background: var(--bg-active, #1a3035); border-color: var(--accent, #18c8da); }
.mc-btn:active { background: var(--accent, #18c8da); }
.mc-role { font-size: 10px; color: var(--accent, #18c8da); font-weight: 600; }
.mc-lbl { font-size: 8px; color: var(--text-dim, #666); font-family: var(--font-mono, monospace); }
.mc-enc {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 5px;
  border: 1px solid var(--border-strong, #333);
  border-radius: var(--radius-xs, 2px);
  background: var(--bg-panel-soft, #252525);
}
.mc-enc .mc-role { min-width: 54px; text-align: center; }
.mc-nudge {
  width: 20px; height: 20px;
  border-radius: var(--radius-xs, 2px);
  border: 1px solid var(--border-strong, #333);
  background: var(--bg-panel, #1e1e1e);
  color: var(--text-base, #aaa);
  font-size: 13px;
  line-height: 1;
  cursor: pointer;
}
.mc-nudge:hover { background: var(--bg-active, #1a3035); color: var(--accent, #18c8da); }
.mc-fader {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 2px 7px;
  border: 1px solid var(--border-strong, #333);
  border-radius: var(--radius-xs, 2px);
  background: var(--bg-panel-soft, #252525);
}
.mc-fader input[type="range"] { width: 90px; accent-color: var(--accent, #18c8da); }

/* ── midi log ────────────────────────────────────────────────────── */
.midi-log {
  background: var(--bg-panel, #1e1e1e);
  border: 1px solid var(--border-strong, #333);
  border-radius: var(--radius-xs, 2px);
  overflow: hidden;
  flex-shrink: 0;
}
.log-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 10px;
  background: var(--bg-shell, #111);
  border-bottom: 1px solid var(--border-strong, #333);
}
.log-title {
  font-size: 9px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.07em;
  color: var(--text-dim, #555);
}
.log-count {
  font-size: 9px;
  color: var(--text-dim, #444);
  background: var(--bg-panel-soft, #222);
  border-radius: 8px;
  padding: 0 5px;
  min-width: 18px;
  text-align: center;
}
.clear-btn {
  margin-left: auto;
  height: 18px;
  padding: 0 8px;
  font-size: 9px;
  border-radius: 2px;
  border: 1px solid var(--border-strong, #333);
  background: transparent;
  color: var(--text-dim, #555);
  cursor: pointer;
}
.clear-btn:hover { color: var(--text-base, #aaa); }
.log-entries {
  max-height: 120px;
  overflow-y: auto;
  padding: 3px 0;
  font-family: var(--font-mono, 'SF Mono', monospace);
  font-size: 10px;
}
.log-entry {
  display: flex;
  gap: 10px;
  padding: 1px 10px;
  line-height: 16px;
}
.log-entry:hover { background: var(--bg-panel-soft, #252525); }
.log-time  { color: var(--text-dim, #555); flex-shrink: 0; }
.log-msg   { color: var(--accent, #18c8da); }
.log-empty {
  padding: 8px 10px;
  color: var(--text-dim, #444);
  font-size: 10px;
  font-family: var(--font-mono, monospace);
}
</style>
