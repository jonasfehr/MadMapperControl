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
const STRIP_W  = 44
const MC_X     = LEFT_W + 4 + 16 * STRIP_W + 4   // middle column left edge (= 770)
const MC_W     = 48                                // middle column button width
const RIGHT_X  = MC_X + MC_W + 8                  // right panel start (= 826)
const DISP_Y       = 8
const DISP_H_CELL  = 34
const SELECT_Y     = 84
const BTN_PITCH    = 30
const MS_Y         = SELECT_Y + 22
const FADER_TOP    = MS_Y + 17
const FADER_BOTTOM = 310
const FADER_H      = FADER_BOTTOM - FADER_TOP
const LABEL_Y      = 324

const STRIP_CENTERS = Array.from({ length: 16 }, (_, i) => LEFT_W + 4 + i * STRIP_W + STRIP_W / 2)

const LEFT_BTN_NAMES  = ['arm', 'solo_clear', 'mute_clear', 'bypass', 'macro', 'link', 'shift']
const LEFT_BTN_COLS   = ['#cc3333', '#cc9922', '#cc6622', '#444', '#444', '#444', '#cc9922']
const LEFT_BTN_LABELS = ['arm', 'solo/clear', 'mute/clear', 'bypass', 'macro', 'link', 'shift']
const MC_UPPER_NAMES  = ['track', 'edit', 'sends']
const MC_UPPER_Y      = [8, 32, 56]           // track at display top; sends ends at 74, 10px gap before pan
const MC_LOWER_NAMES  = ['audio', 'vi', 'bus', 'vca', 'all', 'shift']
const MC_LOWER_Y      = [114, 144, 174, 204, 234, 264]  // aligns 1:1 with solo_clear→shift at BTN_PITCH=30
const AUTO_NAMES     = ['auto_latch', 'auto_trim', 'auto_off', 'auto_touch', 'auto_write', 'auto_read']
const FKEY_NAMES     = ['fkey_chan', 'fkey_zoom', 'fkey_scroll', 'fkey_bank', 'fkey_master', 'fkey_click', 'fkey_section', 'fkey_marker']
const TRANS_NAMES    = ['trans_cycle', 'trans_rewind', 'trans_ffwd', 'trans_stop', 'trans_play', 'trans_record']
const TRANS_COLS     = ['#226666', '#555', '#555', '#664422', '#226633', '#662222']
const TRANS_ICONS    = ['↺', '◀◀', '▶▶', '■', '▶', '●']
const AUTO_LABELS    = AUTO_NAMES.map(n => n.replace('auto_', ''))
const FKEY_LABELS    = FKEY_NAMES.map(n => n.replace('fkey_', ''))

// ── state ─────────────────────────────────────────────────────────
const faderVals   = reactive(Array(16).fill(64))
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
  if (!el || el.address < 0) {
    if (props.mode === 'map' || props.mode === 'learn')
      emit('midi-input', { type: '__learn_click__', name, value: 1 })
    return
  }
  btnState[name] = true
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value, name })
}
function releaseElem(name) {
  const el = elemFor(name)
  if (!el || el.address < 0) return
  btnState[name] = false
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value: 0, name })
}
function toggleElem(name) {
  const el = elemFor(name)
  if (!el || el.address < 0) {
    if (props.mode === 'map' || props.mode === 'learn')
      emit('midi-input', { type: '__learn_click__', name, value: 1 })
    return
  }
  btnState[name] = !btnState[name]
  emit('midi-input', { type: normType(el.type), channel: el.channel, address: el.address, value: btnState[name] ? 127 : 0, name })
}

function chFaderName(i) { return `fader_${i}` }

function sendFader(idx, value) {
  const name = chFaderName(idx + 1)
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
    const name = encIdx === 0 ? 'param' : 'jog'
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
  const name = encIdx === 0 ? 'param' : 'jog'
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
    const name = chFaderName(idx + 1)
    const el = elemFor(name)
    if (el) emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value: 1, name })
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  dragging.value = { idx, startY: e.clientY, startVal: faderVals[idx] }
}
function onMouseMove(e) {
  if (!dragging.value) return
  const { idx, startY, startVal } = dragging.value
  const dy = startY - e.clientY
  faderVals[idx] = Math.max(0, Math.min(127, startVal + Math.round(dy * 127 / FADER_H)))
  sendFader(idx, faderVals[idx])
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
       @mouseenter="hoveredName='param'" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle r="22" :fill="bindingForName('param')?'#1e2428':'#1a2066'"
              :stroke="btnStroke('param','#3355aa')" stroke-width="2"/>
      <circle r="14" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 0" x1="0" y1="-8" x2="0" y2="-18"
            stroke="#4488ff" stroke-width="2.5" stroke-linecap="round"
            :transform="`rotate(${panAngles[0]})`"/>
    </g>

    <!-- Left side buttons -->
    <g v-for="(name, j) in LEFT_BTN_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect x="4" :y="SELECT_Y + j * BTN_PITCH" :width="MC_W" height="18" rx="2"
            :fill="btnState[name] ? LEFT_BTN_COLS[j]+'dd' : (bindingForName(name)?'#1e2428':LEFT_BTN_COLS[j]+'33')"
            :stroke="btnStroke(name, LEFT_BTN_COLS[j])" stroke-width="0.8"/>
      <text :x="4 + MC_W/2" :y="SELECT_Y + j * BTN_PITCH + 9" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="5.5" pointer-events="none"
            :fill="btnState[name] ? '#fff' : LEFT_BTN_COLS[j]">{{ LEFT_BTN_LABELS[j] }}</text>
    </g>

    <!-- ══ CHANNEL STRIPS (16) ══ -->
    <g v-for="i in 16" :key="'ch'+i" :transform="`translate(${STRIP_CENTERS[i-1]}, 0)`">
      <!-- display -->
      <rect :x="-STRIP_W/2+4" :y="DISP_Y" :width="STRIP_W-8" :height="DISP_H_CELL" rx="2"
            fill="#060812" stroke="#1a2a44" stroke-width="0.8"/>
      <template v-if="chLabel(i-1)">
        <text x="0" :y="DISP_Y+13" text-anchor="middle" font-family="monospace" font-size="8"
              fill="#18c8da" opacity="0.85">{{ chLabel(i-1).slice(0,5) }}</text>
        <text x="0" :y="DISP_Y+26" text-anchor="middle" font-family="monospace" font-size="7"
              fill="#18c8da" opacity="0.35">{{ i }}</text>
      </template>

      <!-- Select -->
      <rect :x="-STRIP_W/2+3" :y="SELECT_Y" :width="STRIP_W-6" height="18" rx="2"
            :fill="btnFill(`sel_${i}`, btnState[`sel_${i}`]?'#3a6aaa':'#2a4a77')"
            :stroke="btnStroke(`sel_${i}`,'#336699')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`sel_${i}`)"
            @mouseenter="hoveredName=`sel_${i}`" @mouseleave="hoveredName=null"/>
      <text x="0" :y="SELECT_Y+9" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="5" fill="#4488bb" pointer-events="none">sel</text>

      <!-- Mute -->
      <rect :x="-STRIP_W/2+3" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
            :fill="btnState[`mute_${i}`] ? '#cc3333' : '#771e1e'"
            :stroke="btnStroke(`mute_${i}`,'#993333')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`mute_${i}`)"
            @mouseenter="hoveredName=`mute_${i}`" @mouseleave="hoveredName=null"/>
      <text x="-10.5" :y="MS_Y+6.5" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="6" fill="#bb4444" pointer-events="none">M</text>

      <!-- Solo -->
      <rect :x="4" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
            :fill="btnState[`solo_${i}`] ? '#22aa55' : '#1e5533'"
            :stroke="btnStroke(`solo_${i}`,'#337744')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`solo_${i}`)"
            @mouseenter="hoveredName=`solo_${i}`" @mouseleave="hoveredName=null"/>
      <text x="12.5" :y="MS_Y+6.5" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="6" fill="#33aa66" pointer-events="none">S</text>

      <!-- Fader -->
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#060608" stroke="#1a1a1e"/>
      <rect x="-11" :y="faderY(i-1)-5" width="22" height="10" rx="2"
            fill="#303036" stroke="#505058" style="cursor:ns-resize"
            @mousedown="faderDragStart(i-1, $event)"
            @mouseenter="hoveredName=chFaderName(i)" @mouseleave="hoveredName=null"/>

      <text x="0" :y="LABEL_Y" text-anchor="middle" font-family="monospace" font-size="7" fill="#333">{{ i }}</text>
    </g>

    <!-- ══ MIDDLE COLUMN (10 mode buttons) ══ -->
    <!-- Top 3: track, edit, sends — above SELECT_Y, same h=18 as all other mode buttons -->
    <g v-for="(name, j) in MC_UPPER_NAMES" :key="'mcu'+name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="MC_X" :y="MC_UPPER_Y[j]" :width="MC_W" height="18" rx="2"
            :fill="btnFill(name,'#1a1a1e')" :stroke="btnStroke(name,'#2e2e34')" stroke-width="0.7"/>
      <text :x="MC_X + MC_W/2" :y="MC_UPPER_Y[j] + 9" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="6" fill="#555" pointer-events="none">{{ name }}</text>
    </g>
    <!-- pan — aligns with Select row -->
    <g @click="toggleElem('pan')"
       @mouseenter="hoveredName='pan'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="MC_X" :y="SELECT_Y" :width="MC_W" height="18" rx="2"
            :fill="btnFill('pan','#222228')" :stroke="btnStroke('pan','#333338')" stroke-width="0.7"/>
      <text :x="MC_X + MC_W/2" :y="SELECT_Y + 9" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="6" fill="#666" pointer-events="none">pan</text>
    </g>
    <!-- audio→shift — align with solo_clear→shift on left column -->
    <g v-for="(name, j) in MC_LOWER_NAMES" :key="'mcl'+name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="MC_X" :y="MC_LOWER_Y[j]" :width="MC_W" height="18" rx="2"
            :fill="btnFill(name,'#1a1a20')" :stroke="btnStroke(name,'#2a2a30')" stroke-width="0.7"/>
      <text :x="MC_X + MC_W/2" :y="MC_LOWER_Y[j] + 9" text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="6" fill="#555" pointer-events="none">{{ name }}</text>
    </g>

    <!-- ══ RIGHT PANEL ══ -->
    <rect :x="RIGHT_X" y="8" :width="W - RIGHT_X - 8" :height="H - 16" rx="4"
          fill="#0a0a0c" stroke="#1a1a1e"/>

    <!-- Automation modes -->
    <g v-for="(name, j) in AUTO_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 28 + (j%3)*34" :y="SELECT_Y + Math.floor(j/3)*BTN_PITCH" width="30" height="18" rx="2"
            :fill="btnFill(name, btnState[name]?'#55555599':'#55555544')"
            :stroke="btnStroke(name,'#555')"/>
      <text :x="RIGHT_X + 28 + (j%3)*34 + 15" :y="SELECT_Y + Math.floor(j/3)*BTN_PITCH + 9"
            text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="5.5" fill="#888" pointer-events="none">{{ AUTO_LABELS[j] }}</text>
    </g>

    <!-- Right pan encoder (centered in right panel) -->
    <g :transform="`translate(${RIGHT_X + 77}, 168)`"
       @mousedown="e => startPanDrag(1, rightPanVal, e)"
       @mouseenter="hoveredName='jog'" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle r="20" :fill="bindingForName('jog')?'#1e2428':'#1a2066'"
              :stroke="btnStroke('jog','#3355aa')" stroke-width="1.5"/>
      <circle r="12" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 1" x1="0" y1="-7" x2="0" y2="-16"
            stroke="#4488ff" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${panAngles[1]})`"/>
    </g>

    <!-- Bank nav — arrow-shaped, blue, height=18, flanking the encoder -->
    <!-- bank_down: left-pointing arrow, right tip at encoder_cx - r - 8 -->
    <g style="cursor:pointer" @mousedown="bankDown"
       @mouseenter="hoveredName='bank_down'" @mouseleave="hoveredName=null">
      <polygon :transform="`translate(${RIGHT_X + 28}, 159)`"
               points="0,9 10,0 10,5 22,5 22,13 10,13 10,18"
               :fill="btnFill('bank_down','#1a2a5e')" :stroke="btnStroke('bank_down','#3355cc')" stroke-width="0.8"/>
    </g>
    <!-- bank_up: right-pointing arrow, left tip at encoder_cx + r + 8 -->
    <g style="cursor:pointer" @mousedown="bankUp"
       @mouseenter="hoveredName='bank_up'" @mouseleave="hoveredName=null">
      <polygon :transform="`translate(${RIGHT_X + 104}, 159)`"
               points="22,9 12,0 12,5 0,5 0,13 12,13 12,18"
               :fill="btnFill('bank_up','#1a2a5e')" :stroke="btnStroke('bank_up','#3355cc')" stroke-width="0.8"/>
    </g>

    <!-- F-keys -->
    <g v-for="(name, j) in FKEY_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RIGHT_X + 28 + (j%4)*25" :y="204 + Math.floor(j/4)*BTN_PITCH" width="23" height="18" rx="2"
            :fill="btnFill(name,'#1a1a1e')" :stroke="btnStroke(name,'#2e2e34')"/>
      <text :x="RIGHT_X + 28 + (j%4)*25 + 11.5" :y="204 + Math.floor(j/4)*BTN_PITCH + 9"
            text-anchor="middle" dominant-baseline="central"
            font-family="monospace" font-size="5" fill="#555" pointer-events="none">{{ FKEY_LABELS[j] }}</text>
    </g>

    <!-- Transport -->
    <g v-for="(name, j) in TRANS_NAMES" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <circle :cx="RIGHT_X + 41 + (j%3)*36" :cy="276 + Math.floor(j/3)*40"
              :r="name === 'trans_play' ? 16 : 13"
              :fill="btnFill(name, TRANS_COLS[j]+'55')"
              :stroke="btnStroke(name,TRANS_COLS[j])"/>
      <text :x="RIGHT_X + 41 + (j%3)*36" :y="276 + Math.floor(j/3)*40"
            text-anchor="middle" dominant-baseline="central"
            :font-size="name === 'trans_play' ? 14 : 10"
            :fill="TRANS_COLS[j]" pointer-events="none">{{ TRANS_ICONS[j] }}</text>
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
