<script setup>
import { ref, reactive, computed, watch } from 'vue'

const props = defineProps({
  channels:    { type: Array,   default: () => [] },
  components:  { type: Array,   default: () => [] },
  bindings:    { type: Object,  default: () => ({}) },
  mode:        { type: String,  default: 'emulator' },
  highlighted: { type: String,  default: null },
})
const emit = defineEmits(['midi-input', 'bank-change'])

// ── component map: label → component ──────────────────────────────
const compMap = computed(() => {
  const m = {}
  for (const c of props.components) m[c.label] = c
  return m
})

function normType(t) {
  if (t === 'note_toggle') return 'note'
  if (t === 'encoder_relative' || t === 'encoder' || t === 'cc_toggle') return 'cc'
  return t
}

// ── geometry ──────────────────────────────────────────────────────
const W = 980; const H = 420
const LEFT_W   = 58
const RIGHT_X  = LEFT_W + 14 * 46 + 3 * 38 + 8
const STRIP_W  = 44
const MSTRIP_W = 36
const DISP_Y       = 2
const DISP_H_CELL  = 34
const SELECT_Y     = DISP_Y + DISP_H_CELL + 4
const MS_Y         = SELECT_Y + 22
const FADER_TOP    = MS_Y + 17
const FADER_BOTTOM = 310
const FADER_H      = FADER_BOTTOM - FADER_TOP
const LABEL_Y      = 324

const STRIP_CENTERS = [
  ...Array.from({ length: 14 }, (_, i) => LEFT_W + 4 + i * STRIP_W + STRIP_W / 2),
  ...Array.from({ length: 3 },  (_, i) => LEFT_W + 4 + 14 * STRIP_W + i * MSTRIP_W + MSTRIP_W / 2 + 4),
]
const MASTER_LABELS = ['Spd', 'DMX', 'Vid']
// profile: ch14=fader_Speed, ch15=fader_M_dmx, ch16=fader_M_video
// SVG channel strip 14 (idx 13) maps to fader_Speed, master strips j=1,2 map to fader_M_dmx/video
const MASTER_FADER_NAMES = ['fader_M_dmx', 'fader_M_video', null]

const LEFT_BTN_NAMES = ['arm', 'solo_clear', 'mute_clear', 'bypass', 'macro', 'link', 'shift']
const LEFT_BTN_COLS  = ['#cc3333', '#cc9922', '#cc6622', '#444', '#444', '#444', '#cc9922']
const TP_BTN_NAMES   = ['track', 'timecode', 'edit_fx', 'sends']
const AUTO_NAMES     = ['auto_latch', 'auto_trim', 'auto_off', 'auto_touch', 'auto_write', 'auto_read']
const FKEY_NAMES     = ['fkey_chan', 'fkey_zoom', 'fkey_scroll', 'fkey_bank', 'fkey_all', 'fkey_master', 'fkey_click', 'fkey_section']
const TRANS_NAMES    = ['trans_cycle', 'trans_rewind', 'trans_ffwd', 'trans_stop', 'trans_play', 'trans_record']
const TRANS_COLS     = ['#226666', '#555', '#555', '#664422', '#226633', '#662222']

// ── state ─────────────────────────────────────────────────────────
const faderVals   = reactive(Array(17).fill(64))
const panVal      = ref(64)
const rightPanVal = ref(64)
const btnState    = reactive({})

// ── hover tooltip ─────────────────────────────────────────────────
const svgRef      = ref(null)
const hoveredName = ref(null)
const tooltipPos  = reactive({ x: 0, y: 0 })

function onSVGMouseMove(e) {
  const svg = svgRef.value
  if (!svg || !hoveredName.value) return
  const pt = svg.createSVGPoint()
  pt.x = e.clientX; pt.y = e.clientY
  const svgPt = pt.matrixTransform(svg.getScreenCTM().inverse())
  tooltipPos.x = svgPt.x
  tooltipPos.y = svgPt.y
}

// ── layout helpers ────────────────────────────────────────────────
function elemFor(name) { return compMap.value[name] || null }

function midiKey(el) {
  return `${normType(el.type)}:${el.channel}:${el.address}`
}

const tooltipMidi = computed(() => {
  const el = elemFor(hoveredName.value)
  if (!el) return ''
  const t = normType(el.type)
  return t === 'note'       ? `Note ${el.address} ch${el.channel}`
       : t === 'cc'         ? `CC ${el.address} ch${el.channel}`
       : t === 'pitch_bend' ? `PitchBend ch${el.channel}`
       : ''
})

const tooltipRole = computed(() => {
  const el = elemFor(hoveredName.value)
  if (!el) return '—'
  return props.bindings?.[midiKey(el)]?.role || '—'
})

function bindingForName(name) {
  const el = elemFor(name)
  if (!el) return null
  return props.bindings?.[midiKey(el)] || null
}

function isHighlightedName(name) {
  if (!props.highlighted) return false
  const el = elemFor(name)
  if (!el) return false
  return props.highlighted === midiKey(el)
}

// ── MIDI helpers ──────────────────────────────────────────────────
function pressElem(name, value = 127) {
  const el = elemFor(name)
  if (!el) {
    if (props.mode === 'map' || props.mode === 'learn')
      emit('midi-input', { type: '__learn_click__', name, value: 1 })
    return
  }
  btnState[name] = true
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value, name })
}
function releaseElem(name) {
  const el = elemFor(name)
  if (!el) return
  btnState[name] = false
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value: 0, name })
}
function toggleElem(name) {
  const el = elemFor(name)
  if (!el) {
    if (props.mode === 'map' || props.mode === 'learn')
      emit('midi-input', { type: '__learn_click__', name, value: 1 })
    return
  }
  btnState[name] = !btnState[name]
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value: btnState[name] ? 127 : 0, name })
}

function chFaderName(i) { return i <= 13 ? `fader_${i}` : 'fader_Speed' }

function sendFader(idx, value) {
  const name = chFaderName(idx + 1)
  const el = elemFor(name)
  if (!el) return
  emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value, name })
}
function sendMasterFader(masterIdx, value) {
  const name = MASTER_FADER_NAMES[masterIdx - 1]
  if (!name) return
  const el = elemFor(name)
  if (!el) return
  emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value, name })
}

// ── pan encoder drag ──────────────────────────────────────────────
const panDrag    = ref(null)
const dragPanIdx = ref(-1)
const panAngles  = reactive([0, 0])

function startPanDrag(encIdx, valRef, e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    const name = encIdx === 0 ? 'pan_encoder' : 'right_encoder'
    const el = elemFor(name)
    if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: 1, name })
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  dragPanIdx.value = encIdx
  panDrag.value = { encIdx, valRef, startY: e.clientY, startVal: valRef.value, startAngle: panAngles[encIdx] }
  window.addEventListener('mousemove', onPanMove)
  window.addEventListener('mouseup', onPanUp)
}
function onPanMove(e) {
  if (!panDrag.value) return
  const { encIdx, valRef, startY, startVal, startAngle } = panDrag.value
  const delta = Math.round((startY - e.clientY) * 1.5)
  valRef.value = Math.max(0, Math.min(127, startVal + delta))
  panAngles[encIdx] = ((startAngle + delta * 4) % 360 + 360) % 360
  const name = encIdx === 0 ? 'pan_encoder' : 'right_encoder'
  const el = elemFor(name)
  if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: valRef.value, name })
}
function onPanUp() {
  dragPanIdx.value = -1
  panDrag.value = null
  window.removeEventListener('mousemove', onPanMove)
  window.removeEventListener('mouseup', onPanUp)
}

// ── fader drag ────────────────────────────────────────────────────
function faderY(idx) { return FADER_BOTTOM - (faderVals[idx] / 127) * FADER_H }

const dragging = ref(null)
function faderDragStart(idx, e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    if (idx < 14) {
      const name = chFaderName(idx + 1)
      const el = elemFor(name)
      if (el) emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value: 1, name })
      else emit('midi-input', { type: '__learn_click__', name, value: 1 })
    } else {
      const name = MASTER_FADER_NAMES[idx - 14]
      if (name) {
        const el = elemFor(name)
        if (el) emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value: 1, name })
        else emit('midi-input', { type: '__learn_click__', name, value: 1 })
      }
    }
  }
  dragging.value = { idx, startY: e.clientY, startVal: faderVals[idx] }
}
function onMouseMove(e) {
  if (!dragging.value) return
  const { idx, startY, startVal } = dragging.value
  const dy = startY - e.clientY
  faderVals[idx] = Math.max(0, Math.min(127, startVal + Math.round(dy * 127 / FADER_H)))
  if (idx < 14) sendFader(idx, faderVals[idx])
  else sendMasterFader(idx - 13, faderVals[idx])
}
function onMouseUp() { dragging.value = null }

// ── bank nav ──────────────────────────────────────────────────────
function bankDown() { emit('bank-change', -1); pressElem('bank_down') }
function bankUp()   { emit('bank-change', +1); pressElem('bank_up') }

// ── channel label ─────────────────────────────────────────────────
function chLabel(i) { return props.channels[i]?.label || '' }

// Motorfader: track channel parameter values
watch(() => props.channels, (channels) => {
  channels.forEach((ch, i) => {
    if (ch?.value != null && dragging.value?.idx !== i)
      faderVals[i] = Math.round(ch.value * 127)
  })
}, { immediate: true, deep: true })

// ── colour helpers ────────────────────────────────────────────────
function btnFill(name, defaultFill = '#242428') {
  if (isHighlightedName(name)) return '#243a24'
  if (btnState[name]) return '#18c8da'
  if (bindingForName(name)) return '#1e2428'
  return defaultFill
}
function btnStroke(name, defaultStroke = '#3a3a3c') {
  if (isHighlightedName(name)) return '#a0e060'
  if (bindingForName(name)) return '#18c8da55'
  return defaultStroke
}
</script>

<template>
  <svg ref="svgRef"
       :viewBox="`0 0 ${W} ${H}`" :width="W" :height="H"
       @mousemove="(e) => { onMouseMove(e); onSVGMouseMove(e) }"
       @mouseup="onMouseUp" @mouseleave="onMouseUp"
       style="display:block; user-select:none; cursor:default">

    <!-- device body -->
    <rect x="0" y="0" :width="W" :height="H" rx="10" fill="#0e0e10" stroke="#1e1e22"/>

    <!-- ══ LEFT COLUMN ══ -->
    <!-- Pan encoder -->
    <g transform="translate(29, 32)"
       @mousedown="e => startPanDrag(0, panVal, e)"
       @mouseenter="hoveredName='pan_encoder'" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle r="22" :fill="bindingForName('pan_encoder')?'#1e2428':'#1a2066'"
              :stroke="btnStroke('pan_encoder','#3355aa')" stroke-width="2"/>
      <circle r="14" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 0" x1="0" y1="-8" x2="0" y2="-18"
            stroke="#4488ff" stroke-width="2.5" stroke-linecap="round"
            :transform="`rotate(${panAngles[0]})`"/>
    </g>

    <!-- Left side buttons -->
    <g v-for="(name, j) in LEFT_BTN_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect x="4" :y="66 + j * 46" width="50" height="38" rx="3"
            :fill="btnState[name] ? LEFT_BTN_COLS[j]+'dd' : (bindingForName(name)?'#1e2428':LEFT_BTN_COLS[j]+'33')"
            :stroke="btnStroke(name, LEFT_BTN_COLS[j])" stroke-width="0.8"/>
    </g>

    <!-- ══ CHANNEL STRIPS (14) ══ -->
    <g v-for="i in 14" :key="'ch'+i" :transform="`translate(${STRIP_CENTERS[i-1]}, 0)`">
      <!-- display -->
      <rect :x="-STRIP_W/2+4" :y="DISP_Y" :width="STRIP_W-8" :height="DISP_H_CELL" rx="2"
            fill="#060812" stroke="#1a2a44" stroke-width="0.8"/>
      <template v-if="chLabel(i-1)">
        <text x="0" :y="DISP_Y+13" text-anchor="middle" font-family="monospace" font-size="8"
              fill="#18c8da" opacity="0.85">{{ chLabel(i-1).slice(0,5) }}</text>
        <text x="0" :y="DISP_Y+26" text-anchor="middle" font-family="monospace" font-size="7"
              fill="#18c8da" opacity="0.35">{{ i }}</text>
      </template>

      <!-- Select (profile: sel_1..13, ch14 has no sel) -->
      <rect :x="-STRIP_W/2+3" :y="SELECT_Y" :width="STRIP_W-6" height="18" rx="2"
            :fill="btnFill(`sel_${i}`, btnState[`sel_${i}`]?'#3a6aaa':'#2a4a77')"
            :stroke="btnStroke(`sel_${i}`,'#336699')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`sel_${i}`)"
            @mouseenter="hoveredName=`sel_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Mute -->
      <rect :x="-STRIP_W/2+3" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
            :fill="btnState[`mute_${i}`] ? '#cc3333' : '#771e1e'"
            :stroke="btnStroke(`mute_${i}`,'#993333')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`mute_${i}`)"
            @mouseenter="hoveredName=`mute_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Solo -->
      <rect :x="4" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
            :fill="btnState[`solo_${i}`] ? '#22aa55' : '#1e5533'"
            :stroke="btnStroke(`solo_${i}`,'#337744')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`solo_${i}`)"
            @mouseenter="hoveredName=`solo_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Fader (ch1-13: fader_1..13, ch14: fader_Speed) -->
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#060608" stroke="#1a1a1e"/>
      <rect x="-11" :y="faderY(i-1)-5" width="22" height="10" rx="2"
            fill="#303036" stroke="#505058" style="cursor:ns-resize"
            @mousedown="faderDragStart(i-1, $event)"
            @mouseenter="hoveredName=chFaderName(i)" @mouseleave="hoveredName=null"/>

      <text x="0" :y="LABEL_Y" text-anchor="middle" font-family="monospace" font-size="7" fill="#333">{{ i }}</text>
    </g>

    <!-- ══ MASTER STRIPS (3) ══ -->
    <g v-for="j in 3" :key="'mst'+j" :transform="`translate(${STRIP_CENTERS[13+j]}, 0)`">
      <rect :x="-MSTRIP_W/2+2" :y="DISP_Y" :width="MSTRIP_W-4" :height="DISP_H_CELL" rx="2"
            fill="#0a0e14" stroke="#1a2a44" stroke-width="0.8"/>
      <text x="0" :y="DISP_Y+15" text-anchor="middle" font-family="monospace" font-size="8"
            fill="#18c8da" opacity="0.6">{{ MASTER_LABELS[j-1] }}</text>
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#060608" stroke="#1a1a1e"/>
      <rect x="-11" :y="faderY(13+j)-5" width="22" height="10" rx="2"
            fill="#1e2e3a" stroke="#2a5060" style="cursor:ns-resize"
            @mousedown="faderDragStart(13+j, $event)"
            @mouseenter="hoveredName=MASTER_FADER_NAMES[j-1]" @mouseleave="hoveredName=null"/>
    </g>

    <!-- ══ RIGHT PANEL ══ -->
    <rect :x="RIGHT_X" y="8" :width="W - RIGHT_X - 8" :height="H - 16" rx="4"
          fill="#0a0a0c" stroke="#1a1a1e"/>

    <!-- Top 4 buttons -->
    <g v-for="(name, j) in TP_BTN_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 6" :y="12 + j*26" width="66" height="20" rx="2"
            :fill="btnFill(name,'#222228')" :stroke="btnStroke(name,'#333338')"/>
    </g>

    <!-- Automation modes -->
    <g v-for="(name, j) in AUTO_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 6 + (j%3)*24" :y="120 + Math.floor(j/3)*24" width="22" height="18" rx="2"
            :fill="btnFill(name, btnState[name]?'#55555599':'#55555544')"
            :stroke="btnStroke(name,'#555')"/>
    </g>

    <!-- Right pan encoder -->
    <g :transform="`translate(${RIGHT_X + 39}, 196)`"
       @mousedown="e => startPanDrag(1, rightPanVal, e)"
       @mouseenter="hoveredName='right_encoder'" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle r="20" :fill="bindingForName('right_encoder')?'#1e2428':'#1a2066'"
              :stroke="btnStroke('right_encoder','#3355aa')" stroke-width="1.5"/>
      <circle r="12" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 1" x1="0" y1="-7" x2="0" y2="-16"
            stroke="#4488ff" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${panAngles[1]})`"/>
    </g>

    <!-- Bank nav -->
    <rect :x="RIGHT_X + 6"  y="188" width="22" height="16" rx="2"
          :fill="btnFill('bank_down','#cc8866bb')" :stroke="btnStroke('bank_down','#aa6644')"
          style="cursor:pointer"
          @mousedown="bankDown"
          @mouseenter="hoveredName='bank_down'" @mouseleave="hoveredName=null"/>
    <rect :x="RIGHT_X + 50" y="188" width="22" height="16" rx="2"
          :fill="btnFill('bank_up','#cc8866bb')" :stroke="btnStroke('bank_up','#aa6644')"
          style="cursor:pointer"
          @mousedown="bankUp"
          @mouseenter="hoveredName='bank_up'" @mouseleave="hoveredName=null"/>

    <!-- F-keys -->
    <g v-for="(name, j) in FKEY_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 6 + (j%4)*19" :y="224 + Math.floor(j/4)*20" width="17" height="14" rx="1"
            :fill="btnFill(name,'#1a1a1e')" :stroke="btnStroke(name,'#2e2e34')"/>
    </g>

    <!-- Transport -->
    <g v-for="(name, j) in TRANS_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 6 + (j%3)*25" :y="270 + Math.floor(j/3)*26" width="23" height="20" rx="2"
            :fill="btnFill(name, TRANS_COLS[j]+'55')"
            :stroke="btnStroke(name,TRANS_COLS[j])"/>
    </g>

    <!-- label -->
    <text :x="RIGHT_X + 40" y="340" text-anchor="middle" font-family="monospace"
          font-size="8" font-weight="700" fill="#1e1e22" letter-spacing="2">FADERPORT 16</text>

    <!-- Hover tooltip -->
    <g v-if="hoveredName"
       :transform="`translate(${Math.min(tooltipPos.x + 10, W - 160)}, ${Math.min(tooltipPos.y + 10, H - 55)})`"
       pointer-events="none">
      <rect x="0" y="0" width="150" height="46" rx="3"
            fill="#0d0d10" stroke="#18c8da55" stroke-width="0.8" opacity="0.97"/>
      <text x="7" y="13" font-family="monospace" font-size="8" font-weight="bold" fill="#ddd">{{ hoveredName }}</text>
      <text x="7" y="26" font-family="monospace" font-size="6.5" fill="#18c8da">{{ tooltipMidi }}</text>
      <text x="7" y="38" font-family="monospace" font-size="6.5"
            :fill="tooltipRole === '—' ? '#444' : '#4caf50'">{{ tooltipRole }}</text>
    </g>
  </svg>
</template>
