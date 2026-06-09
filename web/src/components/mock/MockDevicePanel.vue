<script setup>
import { ref, shallowRef, watch, computed, toRef } from 'vue'
import Push3Surface      from './Push3Surface.vue'
import Faderport16Surface from './Faderport16Surface.vue'
import PlatformMSurface   from './PlatformMSurface.vue'
import { useDeviceMappings } from '../../composables/useDeviceMappings.js'

const props = defineProps({
  pages:         { type: Object,  default: () => ({ pages: [], subpages: [] }) },
  config:        { type: Object,  default: () => null },
  allParameters: { type: Array,   default: () => [] },
})

const DEVICES = [
  { id: 'push3',     label: 'Ableton Push 3',  component: Push3Surface,      maxCh: 64 },
  { id: 'fp16',      label: 'Faderport 16',     component: Faderport16Surface, maxCh: 14 },
  { id: 'platformm', label: 'Platform M+',      component: PlatformMSurface,  maxCh: 8  },
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

// ── MIDI log ───────────────────────────────────────────────────────
function onNoteOn(ev)    { log(`Note On  ch:${ev.ch}  note:${ev.note}  vel:${ev.vel}`) }
function onNoteOff(ev)   { log(`Note Off ch:${ev.ch}  note:${ev.note}`) }
function onCC(ev)        { log(`CC       ch:${ev.ch}  cc:${ev.cc}  val:${ev.val}`) }
function onPitchBend(ev) { log(`Pitch    ch:${ev.ch}  val:${ev.val}`) }

const bankLabel = computed(() => {
  const maxCh  = currentDev.value.maxCh
  const total  = resolvedChannels.value.length
  const banks  = Math.max(1, Math.ceil(total / maxCh))
  return `Bank ${bankOffset.value + 1}/${banks}`
})

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
        <span v-if="currentPageName" class="page-badge">
          <span class="page-dot"></span>{{ currentPageName }}
        </span>
        <span v-if="resolvedChannels.length" class="ch-badge">
          {{ resolvedChannels.length }} ch mapped
        </span>
        <span v-if="resolvedChannels.length > currentDev.maxCh" class="bank-badge">
          {{ bankLabel }}
        </span>
      </div>
      <div class="device-select">
        <button
          v-for="d in DEVICES" :key="d.id"
          class="dev-btn"
          :class="{ active: selectedId === d.id }"
          @click="selectedId = d.id"
        >{{ d.label }}</button>
      </div>
    </div>

    <!-- surface -->
    <div class="surface-wrap">
      <component
        :is="currentDev.component"
        :channels="deviceChannels"
        @note-on="onNoteOn"
        @note-off="onNoteOff"
        @cc="onCC"
        @pitch-bend="onPitchBend"
        @bank-change="onBankChange"
      />
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
.device-select { display: flex; gap: 4px; }
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
.dev-btn.active {
  background: var(--bg-active, #1a3035);
  border-color: var(--accent, #18c8da);
  color: var(--accent, #18c8da);
}

/* ── surface ─────────────────────────────────────────────────────── */
.surface-wrap { overflow-x: auto; padding-bottom: 4px; }

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
