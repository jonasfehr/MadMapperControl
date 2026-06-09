<script setup>
import { ref, reactive, computed, watch } from 'vue'

const props = defineProps({
  channels: { type: Array, default: () => [] }
})
const emit = defineEmits(['note-on', 'note-off', 'cc', 'pitch-bend', 'bank-change'])

// ── geometry ──────────────────────────────────────────────────────
const W = 980; const H = 420
const LEFT_W   = 58          // left column
const RIGHT_X  = LEFT_W + 14 * 46 + 3 * 38 + 8  // after all strips
const STRIP_W  = 44          // regular channel strip width
const MSTRIP_W = 36          // master strip width
const DISP_Y       = 2
const DISP_H_CELL  = 34   // tall enough for 2 lines
const SELECT_Y     = DISP_Y + DISP_H_CELL + 4   // = 40
const MS_Y         = SELECT_Y + 22               // = 62
const FADER_TOP    = MS_Y + 17                   // = 79
const FADER_BOTTOM = 310
const FADER_H      = FADER_BOTTOM - FADER_TOP
const LABEL_Y      = 324

// 14 regular ch + 3 master: x_centers
const STRIP_CENTERS = [
  ...Array.from({ length: 14 }, (_, i) => LEFT_W + 4 + i * STRIP_W + STRIP_W / 2),
  ...Array.from({ length: 3 },  (_, i) => LEFT_W + 4 + 14 * STRIP_W + i * MSTRIP_W + MSTRIP_W / 2 + 4),
]
const MASTER_LABELS = ['Spd', 'DMX', 'Vid']

// ── state ─────────────────────────────────────────────────────────
const faderVals   = reactive(Array(17).fill(64))
const panVal      = ref(64)   // left column pan encoder
const rightPanVal = ref(64)   // right panel pan encoder
const btnState    = reactive({})

// ── helpers ───────────────────────────────────────────────────────
function noteOn(note, vel = 127) { emit('note-on',  { note, vel, ch: 1 }) }
function noteOff(note)           { emit('note-off', { note, ch: 1 }) }
function cc(c, v)                { emit('cc',       { cc: c, val: v, ch: 1 }) }

function toggle(key, note) {
  btnState[key] = !btnState[key]
  noteOn(note, btnState[key] ? 127 : 0)
}

function faderY(idx) {
  return FADER_BOTTOM - (faderVals[idx] / 127) * FADER_H
}

// Drag
const dragging = ref(null)
function faderDragStart(idx, e) {
  dragging.value = { idx, startY: e.clientY, startVal: faderVals[idx] }
  e.preventDefault()
}
function onMouseMove(e) {
  if (!dragging.value) return
  const { idx, startY, startVal } = dragging.value
  const dy = startY - e.clientY
  faderVals[idx] = Math.max(0, Math.min(127, startVal + Math.round(dy * 127 / FADER_H)))
  emit('pitch-bend', { ch: idx + 1, val: faderVals[idx] })
}
function onMouseUp() { dragging.value = null }

const panDrag    = ref(null)
const dragPanIdx = ref(-1)     // 0=left encoder, 1=right encoder
const panAngles  = reactive([0, 0])  // visual-only angles, reset on release

function startPanDrag(encoderIdx, valRef, e) {
  e.preventDefault()
  dragPanIdx.value = encoderIdx
  panDrag.value = { encoderIdx, valRef, startY: e.clientY, startVal: valRef.value, startAngle: panAngles[encoderIdx] }
  window.addEventListener('mousemove', onPanMove)
  window.addEventListener('mouseup', onPanUp)
}
function onPanMove(e) {
  if (!panDrag.value) return
  const { encoderIdx, valRef, startY, startVal, startAngle } = panDrag.value
  const delta = Math.round((startY - e.clientY) * 1.5)
  valRef.value = Math.max(0, Math.min(127, startVal + delta))
  panAngles[encoderIdx] = ((startAngle + delta * 4) % 360 + 360) % 360
  cc(0, valRef.value)
}
function onPanUp() {
  dragPanIdx.value = -1
  panDrag.value = null
  window.removeEventListener('mousemove', onPanMove)
  window.removeEventListener('mouseup', onPanUp)
}
function bankDown() { emit('bank-change', -1); noteOn(91) }
function bankUp()   { emit('bank-change', +1); noteOn(92) }

function chLabel(i) {
  const ch = props.channels[i]
  return ch ? ch.label : ''
}

// Motorfader: track channel parameter values
watch(() => props.channels, (channels) => {
  channels.forEach((ch, i) => {
    if (ch?.value != null && dragging.value?.idx !== i) {
      faderVals[i] = Math.round(ch.value * 127)
    }
  })
}, { immediate: true, deep: true })
</script>

<template>
  <svg
    :viewBox="`0 0 ${W} ${H}`"
    :width="W" :height="H"
    @mousemove="onMouseMove"
    @mouseup="onMouseUp"
    @mouseleave="onMouseUp"
    style="display:block; user-select:none; cursor:default"
  >
    <!-- device body -->
    <rect x="0" y="0" :width="W" :height="H" rx="10" fill="#0e0e10" stroke="#1e1e22"/>

    <!-- ══ LEFT COLUMN ═════════════════════════════════════════════ -->
    <!-- Pan/Param encoder (left) -->
    <g transform="translate(29, 32)" @mousedown="e => startPanDrag(0, panVal, e)" style="cursor:ns-resize">
      <circle r="22" fill="#1a2066" stroke="#3355aa" stroke-width="2"/>
      <circle r="14" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 0"
            x1="0" y1="-8" x2="0" y2="-18"
            stroke="#4488ff" stroke-width="2.5" stroke-linecap="round"
            :transform="`rotate(${panAngles[0]})`"/>
      <text x="0" y="4" text-anchor="middle" font-family="monospace" font-size="7" font-weight="700" fill="#4488ff">Pan</text>
    </g>

    <!-- Left side buttons -->
    <g v-for="([label, note, col], j) in [
      ['ARM','arm','#cc3333'],['SoloClear','soloc','#cc9922'],['MuteClear','mutec','#cc6622'],
      ['Bypass','bypass','#444'],['Macro','macro','#444'],['Link','link','#444'],['Shift','shift','#cc9922']
    ]" :key="'lft'+j">
      <rect x="4" :y="66 + j * 46" width="50" height="38" rx="3"
            :fill="btnState[note] ? col+'dd' : col+'33'" :stroke="col" stroke-width="0.8"
            style="cursor:pointer" @click="toggle(note, j+1)"/>
      <text x="29" :y="66 + j*46 + 15" text-anchor="middle" font-family="monospace" font-size="8" font-weight="700"
            :fill="btnState[note] ? '#fff' : '#999'">{{ label.slice(0,4) }}</text>
      <text x="29" :y="66 + j*46 + 27" text-anchor="middle" font-family="monospace" font-size="6"
            :fill="btnState[note] ? '#ccc' : '#666'">{{ label.slice(4) || '' }}</text>
    </g>

    <!-- ══ CHANNEL STRIPS ══════════════════════════════════════════ -->
    <!-- 14 regular channels -->
    <g v-for="i in 14" :key="'ch'+i">
      <g :transform="`translate(${STRIP_CENTERS[i-1]}, 0)`">

        <!-- Select button -->
        <rect :x="-STRIP_W/2+3" :y="SELECT_Y" :width="STRIP_W-6" height="18" rx="2"
              :fill="btnState['sel'+i] ? '#3a6aaa' : '#2a4a77'" stroke="#336699" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('sel'+i, i+23)"/>
        <text x="0" :y="SELECT_Y+12" text-anchor="middle" font-family="monospace" font-size="7" font-weight="600"
              :fill="btnState['sel'+i] ? '#cceeff' : '#88aabb'">Select</text>

        <!-- M+S pair -->
        <rect :x="-STRIP_W/2+3" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
              :fill="btnState['mute'+i] ? '#cc3333' : '#771e1e'" stroke="#993333" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('mute'+i, i+15)"/>
        <text :x="-STRIP_W/2+3+(STRIP_W-10)/4" :y="MS_Y+9" text-anchor="middle"
              font-family="monospace" font-size="7" font-weight="700"
              :fill="btnState['mute'+i] ? '#fff' : '#dd5555'">M</text>

        <rect :x="4" :y="MS_Y" :width="(STRIP_W-10)/2" height="13" rx="1"
              :fill="btnState['solo'+i] ? '#22aa55' : '#1e5533'" stroke="#337744" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('solo'+i, i+7)"/>
        <text :x="4+(STRIP_W-10)/4" :y="MS_Y+9" text-anchor="middle"
              font-family="monospace" font-size="7" font-weight="700"
              :fill="btnState['solo'+i] ? '#fff' : '#44aa66'">S</text>

        <!-- scribble strip display -->
        <rect :x="-STRIP_W/2+4" :y="DISP_Y" :width="STRIP_W-8" :height="DISP_H_CELL" rx="2"
              fill="#060812" stroke="#1a2a44" stroke-width="0.8"/>
        <template v-if="chLabel(i-1)">
          <text x="0" :y="DISP_Y+13" text-anchor="middle" font-family="monospace" font-size="8"
                fill="#18c8da" opacity="0.85">{{ chLabel(i-1).slice(0,5) }}</text>
          <text x="0" :y="DISP_Y+26" text-anchor="middle" font-family="monospace" font-size="7"
                fill="#18c8da" opacity="0.35">{{ i }}</text>
        </template>

        <!-- Fader track -->
        <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3"
              fill="#060608" stroke="#1a1a1e"/>
        <!-- Fader handle -->
        <rect x="-11" :y="faderY(i-1)-5" width="22" height="10" rx="2"
              fill="#303036" stroke="#505058" style="cursor:ns-resize"
              @mousedown="faderDragStart(i-1, $event)"/>

        <!-- channel number -->
        <text x="0" :y="LABEL_Y" text-anchor="middle" font-family="monospace" font-size="7" fill="#444">{{ i }}</text>
      </g>
    </g>

    <!-- 3 master faders -->
    <g v-for="j in 3" :key="'mst'+j">
      <g :transform="`translate(${STRIP_CENTERS[13+j]}, 0)`">
        <!-- display cell -->
        <rect :x="-MSTRIP_W/2+2" :y="DISP_Y" :width="MSTRIP_W-4" :height="DISP_H_CELL" rx="2"
              fill="#0a0e14" stroke="#1a2a44" stroke-width="0.8"/>
        <text x="0" :y="DISP_Y+15" text-anchor="middle" font-family="monospace" font-size="8"
              fill="#18c8da" opacity="0.6">{{ MASTER_LABELS[j-1] }}</text>
        <!-- fader -->
        <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#060608" stroke="#1a1a1e"/>
        <rect x="-11" :y="faderY(13+j)-5" width="22" height="10" rx="2"
              fill="#1e2e3a" stroke="#2a5060" style="cursor:ns-resize"
              @mousedown="faderDragStart(13+j, $event)"/>
      </g>
    </g>

    <!-- ══ RIGHT PANEL ════════════════════════════════════════════ -->
    <rect :x="RIGHT_X" y="8" :width="W - RIGHT_X - 8" :height="H - 16" rx="4"
          fill="#0a0a0c" stroke="#1a1a1e"/>

    <!-- Top 4 buttons -->
    <g v-for="([label, note], j) in [['Track',100],['Timecode',101],['Edit Fx',102],['Sends',103]]" :key="'tp'+j">
      <rect :x="RIGHT_X + 6" :y="12 + j*26" width="66" height="20" rx="2"
            fill="#222228" stroke="#333338" style="cursor:pointer" @click="toggle('tp'+j, note)"/>
      <text :x="RIGHT_X + 39" :y="26 + j*26" text-anchor="middle" font-family="monospace" font-size="7" fill="#777">{{ label }}</text>
    </g>

    <!-- Automation modes -->
    <g v-for="([label, note, col], j) in [['Latch',104,'#555'],['Trim',105,'#555'],['Off',106,'#555'],['Touch',107,'#226666'],['Write',81,'#226633'],['Read',83,'#226633']]" :key="'au'+j">
      <rect :x="RIGHT_X + 6 + (j%3)*24" :y="120 + Math.floor(j/3)*24" width="22" height="18" rx="2"
            :fill="btnState['au'+j] ? col+'dd' : col+'44'" :stroke="col"
            style="cursor:pointer" @click="toggle('au'+j, note)"/>
      <text :x="RIGHT_X + 17 + (j%3)*24" :y="133 + Math.floor(j/3)*24"
            text-anchor="middle" font-family="monospace" font-size="6"
            :fill="btnState['au'+j] ? '#fff' : '#888'">{{ label.slice(0,5) }}</text>
    </g>

    <!-- Pan encoder (right panel, independent from left) -->
    <g :transform="`translate(${RIGHT_X + 39}, 196)`" @mousedown="e => startPanDrag(1, rightPanVal, e)" style="cursor:ns-resize">
      <circle r="20" fill="#1a2066" stroke="#3355aa" stroke-width="1.5"/>
      <circle r="12" fill="none" stroke="#2233aa" stroke-width="1"/>
      <line v-if="dragPanIdx === 1"
            x1="0" y1="-7" x2="0" y2="-16"
            stroke="#4488ff" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${panAngles[1]})`"/>
    </g>
    <!-- Prev/Next bank -->
    <rect :x="RIGHT_X + 6"  y="188" width="22" height="16" rx="2" fill="#cc8866bb" stroke="#aa6644"
          style="cursor:pointer" @mousedown="bankDown"/>
    <text :x="RIGHT_X + 17" y="200" text-anchor="middle" font-family="monospace" font-size="10" fill="#2a1a10">‹</text>
    <rect :x="RIGHT_X + 50" y="188" width="22" height="16" rx="2" fill="#cc8866bb" stroke="#aa6644"
          style="cursor:pointer" @mousedown="bankUp"/>
    <text :x="RIGHT_X + 61" y="200" text-anchor="middle" font-family="monospace" font-size="10" fill="#2a1a10">›</text>

    <!-- F-key navigation -->
    <g v-for="([label, note], j) in [['Chan',106],['Zoom',107],['Scroll',108],['Bank',92],['All',109],['Master',110],['Click',111],['Section',112]]" :key="'fk'+j">
      <rect :x="RIGHT_X + 6 + (j%4)*19" :y="224 + Math.floor(j/4)*20" width="17" height="14" rx="1"
            fill="#1a1a1e" stroke="#2e2e34" style="cursor:pointer" @click="toggle('fk'+j, note)"/>
      <text :x="RIGHT_X + 14.5 + (j%4)*19" :y="235 + Math.floor(j/4)*20"
            text-anchor="middle" font-family="monospace" font-size="5.5" fill="#666">{{ label.slice(0,4) }}</text>
    </g>

    <!-- Transport -->
    <g v-for="([label, note, col], j) in [['⇄',86,'#226666'],['◀◀',91,'#555'],['▶▶',92,'#555'],['■',93,'#664422'],['▶‖',94,'#226633'],['●',95,'#662222']]" :key="'tsp'+j">
      <rect :x="RIGHT_X + 6 + (j%3)*25" :y="270 + Math.floor(j/3)*26" width="23" height="20" rx="2"
            :fill="btnState['tsp'+j] ? col+'ee' : col+'55'" :stroke="col"
            style="cursor:pointer" @click="toggle('tsp'+j, note)"/>
      <text :x="RIGHT_X + 17.5 + (j%3)*25" :y="284 + Math.floor(j/3)*26"
            text-anchor="middle" font-family="monospace" font-size="10"
            :fill="btnState['tsp'+j] ? '#fff' : '#999'">{{ label }}</text>
    </g>

    <!-- FADERPORT 16 label -->
    <text :x="RIGHT_X + 40" y="340" text-anchor="middle" font-family="monospace" font-size="8" font-weight="700" fill="#1e1e22" letter-spacing="2">FADERPORT 16</text>
  </svg>
</template>
