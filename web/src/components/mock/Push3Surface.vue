<script setup>
import { ref, reactive, computed } from 'vue'

const props = defineProps({
  channels: {
    type: Array,
    default: () => [],
  },
})

const emit = defineEmits(['note-on', 'note-off', 'cc', 'pitch-bend', 'bank-change'])

// ── state ─────────────────────────────────────────────────────────
const encVals       = reactive(Array(8).fill(64))
const encAngles     = reactive(Array(8).fill(0))   // visual-only, reset after drag
const dragEncIdx    = ref(-1)
const volVal        = ref(64)
const swingVal      = ref(64)
const jogVal        = ref(64)
const bigKnobAngles = reactive({ vol: 0, swing: 0, jog: 0 })
const bigKnobDragKey = ref(null)  // 'vol' | 'swing' | 'jog' | null
const btnState      = reactive({})
const padColors     = ref(Array(64).fill(null))

// ── helpers ───────────────────────────────────────────────────────
function noteOn(note, vel = 127)  { emit('note-on',  { note, vel, ch: 1 }) }
function noteOff(note)            { emit('note-off', { note, ch: 1 }) }
function cc(c, v)                 { emit('cc',       { cc: c, val: v, ch: 1 }) }

function toggleBtn(key, note) {
  btnState[key] = !btnState[key]
  noteOn(note, btnState[key] ? 127 : 0)
}

// ── encoder / knob drag ───────────────────────────────────────────
const knobDrag = ref(null)

function startEncDrag(idx, e) {
  e.preventDefault()
  dragEncIdx.value = idx
  knobDrag.value = { idx, isEnc: true, startY: e.clientY, startVal: encVals[idx], startAngle: encAngles[idx], ccNum: 71 + idx }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function startBigKnobDrag(valRef, ccNum, key, e) {
  e.preventDefault()
  bigKnobDragKey.value = key
  knobDrag.value = { valRef, isEnc: false, startY: e.clientY, startVal: valRef.value, startAngle: bigKnobAngles[key], ccNum, key }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function onKnobMove(e) {
  if (!knobDrag.value) return
  const { idx, valRef, isEnc, startY, startVal, startAngle, ccNum, key } = knobDrag.value
  const delta = Math.round((startY - e.clientY) * 1.5)
  if (isEnc) {
    encVals[idx] = Math.max(0, Math.min(127, startVal + delta))
    encAngles[idx] = ((startAngle + delta * 4) % 360 + 360) % 360
    cc(ccNum, encVals[idx])
  } else {
    valRef.value = Math.max(0, Math.min(127, startVal + delta))
    bigKnobAngles[key] = ((startAngle + delta * 4) % 360 + 360) % 360
    cc(ccNum, valRef.value)
  }
}
function onKnobUp() {
  dragEncIdx.value = -1
  bigKnobDragKey.value = null
  knobDrag.value = null
  window.removeEventListener('mousemove', onKnobMove)
  window.removeEventListener('mouseup', onKnobUp)
}

// bank navigation (emits bank-change so parent updates channel offset)
function bankDown() { emit('bank-change', -1); noteOn(62) }
function bankUp()   { emit('bank-change', +1); noteOn(63) }

// knobAngle: maps 0-127 → -135..+135 degrees
function knobAngle(v) {
  return (v / 127) * 270 - 135
}

// ── touch strip (vertical, left of pad grid) ─────────────────────
const tsActive  = ref(false)
const tsPos     = ref(0)     // 0=bottom 1=top
const tsRectRef = ref(null)

function tsStart(e) {
  tsActive.value = true
  tsCompute(e)
  window.addEventListener('mousemove', tsMove)
  window.addEventListener('mouseup', tsEnd)
}
function tsMove(e) { if (tsActive.value) tsCompute(e) }
function tsEnd()   {
  tsActive.value = false
  window.removeEventListener('mousemove', tsMove)
  window.removeEventListener('mouseup', tsEnd)
}
function tsCompute(e) {
  if (!tsRectRef.value) return
  const r = tsRectRef.value.getBoundingClientRect()
  const y = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height))
  tsPos.value = 1 - y
  cc(12, Math.round((1 - y) * 127))
}


// ── display labels ────────────────────────────────────────────────
const displaySlots = computed(() => {
  const slots = []
  for (let i = 0; i < 8; i++) {
    const ch = props.channels[i]
    slots.push(ch ? (ch.label || ch.path || '') : '')
  }
  return slots
})

function truncate(s, n = 10) {
  if (!s) return ''
  return s.length > n ? s.slice(0, n - 1) + '…' : s
}

// Value bar: normalize 0-1 float (OSCQuery) → pixel width
function barW(val, maxW) {
  if (val == null) return 0
  return Math.max(0, Math.min(maxW, val * maxW))
}

// ── expose for parent ─────────────────────────────────────────────
defineExpose({
  setPadColor(idx, color) { padColors.value[idx] = color },
  clearPads()             { padColors.value = Array(64).fill(null) },
})

// ── SVG layout constants ──────────────────────────────────────────
// viewBox 0 0 880 560
// Device body rect: x=6, y=6, w=868, h=548, rx=18

// Left panel:  x=14..140
// Center:      x=148..718
// Right panel: x=726..862

// Center sub-layout:
//   encoders row y=22  (8 knobs r=11 spaced 71px, start x=148+10=158 center)
//   upper disp btns y=54
//   main display y=76  h=68
//   lower disp btns y=152
//   pad area y=182  (pads 8×8, landscape, aligned with encoder columns)
//   h-touch strip y=472

const ENC_Y = 34          // center y of encoder row
const ENC_R = 11          // encoder knob radius
const ENC_IND = 8         // indicator line length from center
const ENC_SPACING = 71    // spacing between encoders
const ENC_START_X = 183   // x of first encoder center

const DISP_BTN_Y = 60     // y top of upper display buttons
const DISP_BTN_H = 16
const DISP_BTN_W = 67     // 67 + 4 gap = 71 = encoder/pad column stride
const DISP_START_X = 151  // x of first disp button

const DISPLAY_X = 151
const DISPLAY_Y = 82
const DISPLAY_W = 568
const DISPLAY_H = 62

const LOWER_BTN_Y = 150   // y top of lower display buttons

const PAD_AREA_Y = 176
const PAD_AREA_X = DISPLAY_X
// Pads: landscape (wider than tall), 8 cols × 71 px = 568 px = DISPLAY_W
const PAD_W    = 67   // width per pad  (PAD_W + PAD_GAP = 71 = encoder spacing)
const PAD_H    = 34   // height per pad
const PAD_GAP  = 4
const PAD_COLS = 8

const PADS_X = DISPLAY_X        // align left edge with display / encoders
const PADS_Y = PAD_AREA_Y

const PAD_GRID_H = PAD_COLS * (PAD_H + PAD_GAP) - PAD_GAP  // 300px

// Touch strip: vertical, left of pad grid
const TS_X = DISPLAY_X - 14   // 10px strip + 4px gap before pads
const TS_Y = PAD_AREA_Y
const TS_W = 10
const TS_H = PAD_GRID_H

// Scene launch buttons: right of pad grid (narrow arrows, RP_X=730)
const SCENES_X    = PADS_X + PAD_COLS * (PAD_W + PAD_GAP) - PAD_GAP + 3  // =718
const SCENE_BTN_W = 10   // 718+10=728, 2px gap before RP_X=730
const SCENE_BTN_H = PAD_H

// Right panel button layout
const RP_X = 730
const RP_BTN_W = 58
const RP_BTN_H = 18

// Left panel button layout
const LP_X = 16
const LP_BTN_W = 58
const LP_BTN_H = 18

// Large knob helpers
function bigKnobCx(id) {
  if (id === 'vol')   return 45
  if (id === 'swing') return 45
  if (id === 'jog')   return 847
  return 0
}

// Pad index to x,y
function padX(i) { return PADS_X + (i % 8) * (PAD_W + PAD_GAP) }
function padY(i) { return PADS_Y + Math.floor(i / 8) * (PAD_H + PAD_GAP) }

// ── left panel button list ────────────────────────────────────────
// Each: { label, note, y }
const LP_BTNS_TOP = [
  { label: 'USER',        note: 59  },
  { label: 'LEARN',       note: 116 },
  { label: 'SETUP',       note: 76  },
]
const LP_BTNS_MID = [
  { label: 'MUTE',        note: 61  },
  { label: 'STOP CLIP',   note: 62  },
  { label: 'SOLO',        note: 66  },
]
const LP_BTNS_BOT = [
  { label: 'TAP TEMPO',   note: 88  },
  { label: 'METRONOME',   note: 89  },
  { label: 'QUANTIZE',    note: 90  },
  { label: 'FIXED LEN',   note: 91  },
  { label: 'AUTOMATE',    note: 92  },
  { label: 'NEW',         note: 93  },
  { label: 'CAPTURE',     note: 94  },
  { label: 'RECORD',      note: 95  },
  { label: 'PLAY',        note: 96  },
]

// Left small 4-button rows
const LP_SMALL_TOP = [72, 73, 74, 75]
const LP_SMALL_MID = [80, 81, 82, 83]

// Right panel
const RP_BTNS_TOP = [
  { label: 'DEVICE',      note: 110 },
  { label: 'MIX',         note: 111 },
  { label: 'SESSION',     note: 112 },
  { label: 'CLIP',        note: 113 },
]
const RP_BTNS_MID = [
  { label: 'SWAP',        note: 121 },
  { label: 'MASTER TRK',  note: 122 },
]
const RP_BTNS_BOT = [
  { label: 'SESSION',     note: 123 },
  { label: 'NOTE',        note: 124 },
  { label: 'SCALE',       note: 125 },
  { label: 'LAYOUT',      note: 126 },
  { label: 'REPEAT',      note: 56  },
  { label: 'ACCENT',      note: 57  },
  { label: 'DBL LOOP',    note: 52  },
  { label: 'DUP BEATS',   note: 53  },
  { label: 'CONVERT',     note: 54  },
  { label: 'DELETE',      note: 55  },
  { label: 'OCT ↑',       note: 84  },
  { label: 'OCT ↓',       note: 85  },
  { label: '◀ PAGE',      note: 62  },
  { label: 'PAGE ▶',      note: 63  },
  { label: 'SHIFT',       note: 49  },
  { label: 'SELECT',      note: 48  },
]

// Compute y positions for left panel buttons
function lpBtnY(group, idx) {
  if (group === 'top') return 20 + idx * 22
  if (group === 'small-top') return 20 + 3 * 22 + 4 + idx * 0  // inline row
  if (group === 'vol') return 20 + 3 * 22 + 28
  if (group === 'mid') return 20 + 3 * 22 + 78 + idx * 22
  if (group === 'small-mid') return 20 + 3 * 22 + 78 + 3 * 22 + 4
  if (group === 'swing') return 20 + 3 * 22 + 78 + 3 * 22 + 28
  if (group === 'bot') return 20 + 3 * 22 + 78 + 3 * 22 + 78 + idx * 20
  return 0
}
</script>

<template>
  <!-- SVG-based Push 3 technical schematic mock -->
  <svg
    viewBox="0 0 880 560"
    xmlns="http://www.w3.org/2000/svg"
    class="push3-svg"
    :style="{ userSelect: 'none' }"
  >
    <!-- ── Device body ──────────────────────────────────────────── -->
    <rect x="4" y="4" width="872" height="552" rx="20" ry="20"
          fill="#111113" stroke="#2a2a2e" stroke-width="1.5"/>

    <!-- ═══════════════════════════════════════════════════════════
         LEFT PANEL
    ════════════════════════════════════════════════════════════════ -->

    <!-- Section label -->
    <text x="74" y="16" text-anchor="middle" font-family="monospace" font-size="7" fill="#333">LEFT CONTROLS</text>

    <!-- USER -->
    <g @mousedown="noteOn(59)" @mouseup="noteOff(59)" style="cursor:pointer">
      <rect :x="LP_X" y="20" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[59] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="20 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[59] ? '#000' : '#888'">USER</text>
    </g>
    <!-- LEARN -->
    <g @mousedown="noteOn(116)" @mouseup="noteOff(116)" style="cursor:pointer">
      <rect :x="LP_X" y="42" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[116] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="42 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[116] ? '#000' : '#888'">LEARN</text>
    </g>

    <!-- Sets label + 4 small buttons inline -->
    <text :x="LP_X" y="73" font-family="monospace" font-size="6" fill="#444">SETS</text>
    <g v-for="(note, si) in LP_SMALL_TOP" :key="'lsm-top-'+si"
       @mousedown="noteOn(note)" @mouseup="noteOff(note)" style="cursor:pointer">
      <rect :x="LP_X + si * 15" y="76" width="13" height="13" rx="2"
            :fill="btnState[note] ? '#18c8da' : '#1e1e22'" stroke="#3a3a3c" stroke-width="0.7"/>
    </g>

    <!-- SETUP -->
    <g @mousedown="noteOn(76)" @mouseup="noteOff(76)" style="cursor:pointer">
      <rect :x="LP_X" y="93" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[76] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="93 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[76] ? '#000' : '#888'">SETUP</text>
    </g>

    <!-- Volume knob -->
    <text :x="LP_X + LP_BTN_W + 2" y="130" font-family="monospace" font-size="6" fill="#444">UNDO</text>
    <g :transform="`translate(${LP_X + 29}, 130)`" @mousedown="e => startBigKnobDrag(volVal, 7, 'vol', e)" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="16" fill="#1e1e20" stroke="#484848" stroke-width="1.2"/>
      <circle cx="0" cy="0" r="13" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey === 'vol'"
            x1="0" y1="-5" x2="0" y2="-13"
            stroke="#18c8da" stroke-width="1.8" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.vol})`"/>
    </g>
    <text :x="LP_X + 29" y="151" text-anchor="middle" font-family="monospace" font-size="6" fill="#555">VOLUME</text>
    <text :x="LP_X + LP_BTN_W + 2" y="145" font-family="monospace" font-size="6" fill="#444">SAVE</text>

    <!-- MUTE -->
    <g @mousedown="noteOn(61)" @mouseup="noteOff(61)" style="cursor:pointer">
      <rect :x="LP_X" y="158" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[61] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="158 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[61] ? '#000' : '#888'">MUTE</text>
    </g>
    <!-- STOP CLIP -->
    <g @mousedown="noteOn(62)" @mouseup="noteOff(62)" style="cursor:pointer">
      <rect :x="LP_X" y="180" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[62] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="180 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[62] ? '#000' : '#888'">STOP CLIP</text>
    </g>

    <!-- Lock + 4 small -->
    <text :x="LP_X" y="211" font-family="monospace" font-size="6" fill="#444">LOCK</text>
    <g v-for="(note, si) in LP_SMALL_MID" :key="'lsm-mid-'+si"
       @mousedown="noteOn(note)" @mouseup="noteOff(note)" style="cursor:pointer">
      <rect :x="LP_X + si * 15" y="213" width="13" height="13" rx="2"
            :fill="btnState[note] ? '#18c8da' : '#1e1e22'" stroke="#3a3a3c" stroke-width="0.7"/>
    </g>

    <!-- SOLO -->
    <g @mousedown="noteOn(66)" @mouseup="noteOff(66)" style="cursor:pointer">
      <rect :x="LP_X" y="230" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[66] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="230 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[66] ? '#000' : '#888'">SOLO</text>
    </g>

    <!-- Swing knob -->
    <g :transform="`translate(${LP_X + 29}, 270)`" @mousedown="e => startBigKnobDrag(swingVal, 17, 'swing', e)" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="16" fill="#1e1e20" stroke="#484848" stroke-width="1.2"/>
      <circle cx="0" cy="0" r="13" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey === 'swing'"
            x1="0" y1="-5" x2="0" y2="-13"
            stroke="#18c8da" stroke-width="1.8" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.swing})`"/>
    </g>
    <text :x="LP_X + 29" y="291" text-anchor="middle" font-family="monospace" font-size="6" fill="#555">SWING &amp; TEMPO</text>

    <!-- Bottom left buttons stacked -->
    <g v-for="(btn, bi) in LP_BTNS_BOT" :key="'lbot-'+bi"
       @mousedown="noteOn(btn.note)" @mouseup="noteOff(btn.note)" style="cursor:pointer">
      <rect :x="LP_X" :y="300 + bi * 22" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnState[btn.note] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="LP_X + LP_BTN_W/2" :y="300 + bi * 22 + LP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7" :fill="btnState[btn.note] ? '#000' : '#888'">{{ btn.label }}</text>
    </g>

    <!-- ═══════════════════════════════════════════════════════════
         CENTER PANEL
    ════════════════════════════════════════════════════════════════ -->

    <!-- ── 8 Encoders ───────────────────────────────────────────── -->
    <g v-for="i in 8" :key="'enc-'+i"
       :transform="`translate(${ENC_START_X + (i-1)*ENC_SPACING}, ${ENC_Y})`"
       @mousedown="e => startEncDrag(i-1, e)"
       style="cursor:ns-resize">
      <!-- knob body -->
      <circle cx="0" cy="0" :r="ENC_R" fill="#1e1e20" stroke="#484848" stroke-width="1"/>
      <circle cx="0" cy="0" :r="ENC_R - 3" fill="none" stroke="#2e2e32" stroke-width="0.6"/>
      <!-- indicator only visible while dragging this encoder -->
      <line v-if="dragEncIdx === i-1"
            x1="0" y1="-3" :x2="0" :y2="-(ENC_R - 2)"
            stroke="#18c8da" stroke-width="1.5" stroke-linecap="round"
            :transform="`rotate(${encAngles[i-1]})`"/>
      <!-- channel label below knob -->
      <text x="0" :y="ENC_R + 9" text-anchor="middle"
            font-family="monospace" font-size="6" fill="#555">
        {{ truncate(displaySlots[i-1], 7) }}
      </text>
    </g>

    <!-- ── Upper display buttons (CC 102-109 per Push2/3 spec) ─── -->
    <g v-for="i in 8" :key="'udb-'+i"
       @mousedown="cc(101+i, 127)" @mouseup="cc(101+i, 0)" style="cursor:pointer">
      <rect :x="DISP_START_X + (i-1)*(DISP_BTN_W + 4)" :y="DISP_BTN_Y"
            :width="DISP_BTN_W" :height="DISP_BTN_H" rx="2"
            :fill="btnState['udb'+i] ? '#18c8da' : '#1e1e22'" stroke="#353538" stroke-width="0.8"/>
      <text :x="DISP_START_X + (i-1)*(DISP_BTN_W + 4) + DISP_BTN_W/2"
            :y="DISP_BTN_Y + DISP_BTN_H/2 + 3" text-anchor="middle"
            font-family="monospace" font-size="6" :fill="btnState['udb'+i] ? '#000' : '#555'">
        {{ truncate(displaySlots[i-1], 8) }}
      </text>
    </g>

    <!-- ── Main display ─────────────────────────────────────────── -->
    <rect :x="DISPLAY_X" :y="DISPLAY_Y" :width="DISPLAY_W" :height="DISPLAY_H"
          rx="3" fill="#050508" stroke="#1e1e22" stroke-width="1"/>
    <!-- Display content: 8 channel slots -->
    <g v-for="i in 8" :key="'dsp-'+i">
      <!-- channel label — only when mapped -->
      <text v-if="displaySlots[i-1]"
            :x="DISPLAY_X + 10 + (i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y + 18"
            font-family="monospace" font-size="7" fill="#18c8da" opacity="0.7">
        {{ truncate(displaySlots[i-1], 9) }}
      </text>
      <!-- value bar — only when channel is mapped -->
      <template v-if="channels[i-1]">
        <rect :x="DISPLAY_X + 4 + (i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y + 28"
              :width="DISPLAY_W/8 - 8" height="8" rx="1.5"
              fill="#0a0f12"/>
        <rect v-if="channels[i-1].value != null"
              :x="DISPLAY_X + 4 + (i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y + 28"
              :width="barW(channels[i-1].value, DISPLAY_W/8 - 8)"
              height="8" rx="1.5"
              fill="#18c8da" opacity="0.65"/>
      </template>
      <!-- divider -->
      <line v-if="i < 8"
            :x1="DISPLAY_X + i*(DISPLAY_W/8)" :y1="DISPLAY_Y + 4"
            :x2="DISPLAY_X + i*(DISPLAY_W/8)" :y2="DISPLAY_Y + DISPLAY_H - 4"
            stroke="#1e1e26" stroke-width="0.5"/>
    </g>
    <!-- Push 3 branding -->
    <text :x="DISPLAY_X + DISPLAY_W/2" :y="DISPLAY_Y + DISPLAY_H - 8"
          text-anchor="middle" font-family="monospace" font-size="7" fill="#18c8da" opacity="0.2"
          letter-spacing="3">PUSH 3</text>

    <!-- ── Lower display buttons (CC 20-27 per Push2/3 spec) ───── -->
    <g v-for="i in 8" :key="'ldb-'+i"
       @mousedown="cc(19+i, 127)" @mouseup="cc(19+i, 0)" style="cursor:pointer">
      <rect :x="DISP_START_X + (i-1)*(DISP_BTN_W + 4)" :y="LOWER_BTN_Y"
            :width="DISP_BTN_W" :height="DISP_BTN_H" rx="2"
            :fill="btnState['ldb'+i] ? '#18c8da' : '#1e1e22'" stroke="#353538" stroke-width="0.8"/>
    </g>

    <!-- ── Touch strip (vertical, left of pad grid) ─────────────── -->
    <rect ref="tsRectRef"
          :x="TS_X" :y="TS_Y" :width="TS_W" :height="TS_H"
          rx="4" fill="#0a0a0e" stroke="#222" stroke-width="0.8"
          style="cursor:ns-resize"
          @mousedown.prevent="tsStart"/>
    <rect :x="TS_X + 2" :y="TS_Y + 2 + (1 - tsPos) * (TS_H - 4)"
          :width="TS_W - 4" :height="tsPos * (TS_H - 4)"
          rx="2" fill="#18c8da" opacity="0.4"/>
    <text :x="TS_X + TS_W / 2" :y="TS_Y + TS_H + 10"
          text-anchor="middle" font-family="monospace" font-size="6" fill="#333" writing-mode="horizontal-tb">TOUCH</text>

    <!-- ── Pad grid (8×8) — landscape pads aligned with encoders ── -->
    <g v-for="i in 64" :key="'pad-'+i"
       @mousedown="noteOn(35+i)" @mouseup="noteOff(35+i)" style="cursor:pointer">
      <rect
        :x="padX(i-1)" :y="padY(i-1)"
        :width="PAD_W" :height="PAD_H"
        rx="2"
        :fill="padColors[i-1] || '#1a1a1e'"
        :stroke="padColors[i-1] ? 'none' : '#2a2a2e'"
        stroke-width="0.8"
      />
    </g>

    <!-- ── Scene launch buttons (right of pads) ─────────────────── -->
    <g v-for="i in 8" :key="'scene-'+i"
       @mousedown="noteOn(43+i)" @mouseup="noteOff(43+i)" style="cursor:pointer">
      <rect :x="SCENES_X" :y="PADS_Y + (i-1)*(SCENE_BTN_H + PAD_GAP)"
            :width="SCENE_BTN_W" :height="SCENE_BTN_H" rx="2"
            :fill="btnState[43+i] ? '#18c8da' : '#1e1e22'" stroke="#2a2a2e" stroke-width="0.8"/>
      <text :x="SCENES_X + SCENE_BTN_W/2"
            :y="PADS_Y + (i-1)*(SCENE_BTN_H + PAD_GAP) + SCENE_BTN_H/2 + 3"
            text-anchor="middle" font-family="monospace" font-size="7"
            :fill="btnState[43+i] ? '#000' : '#444'">▶</text>
    </g>

    <!-- ═══════════════════════════════════════════════════════════
         RIGHT PANEL
    ════════════════════════════════════════════════════════════════ -->
    <text :x="RP_X + RP_BTN_W/2" y="16" text-anchor="middle"
          font-family="monospace" font-size="7" fill="#333">RIGHT CONTROLS</text>

    <!-- Top right buttons -->
    <g v-for="(btn, bi) in RP_BTNS_TOP" :key="'rp-top-'+bi"
       @mousedown="noteOn(btn.note)" @mouseup="noteOff(btn.note)" style="cursor:pointer">
      <rect :x="RP_X" :y="20 + bi * 22" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            :fill="btnState[btn.note] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="RP_X + RP_BTN_W/2" :y="20 + bi * 22 + RP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7.5" :fill="btnState[btn.note] ? '#000' : '#888'">{{ btn.label }}</text>
    </g>

    <!-- +ADD button -->
    <g @mousedown="noteOn(114)" @mouseup="noteOff(114)" style="cursor:pointer">
      <rect :x="RP_X" y="110" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            fill="#1e3a3e" stroke="#18c8da" stroke-width="0.8"/>
      <text :x="RP_X + RP_BTN_W/2" y="122" text-anchor="middle"
            font-family="monospace" font-size="10" fill="#18c8da">+ADD</text>
    </g>

    <!-- Jog wheel -->
    <g :transform="`translate(${RP_X + 34}, 163)`"
       @mousedown="e => startBigKnobDrag(jogVal, 60, 'jog', e)" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="22" fill="#1e1e20" stroke="#484848" stroke-width="1.5"/>
      <circle cx="0" cy="0" r="18" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <circle cx="0" cy="0" r="8"  fill="#181818" stroke="#3a3a3c" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey === 'jog'"
            x1="0" y1="-6" x2="0" y2="-17"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.jog})`"/>
    </g>
    <text :x="RP_X + 34" y="191" text-anchor="middle"
          font-family="monospace" font-size="6" fill="#555">JOG</text>

    <!-- SWAP / MASTER TRACK -->
    <g v-for="(btn, bi) in RP_BTNS_MID" :key="'rp-mid-'+bi"
       @mousedown="noteOn(btn.note)" @mouseup="noteOff(btn.note)" style="cursor:pointer">
      <rect :x="RP_X" :y="198 + bi * 22" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            :fill="btnState[btn.note] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.8"/>
      <text :x="RP_X + RP_BTN_W/2" :y="198 + bi * 22 + RP_BTN_H/2 + 3.5" text-anchor="middle"
            font-family="monospace" font-size="7" :fill="btnState[btn.note] ? '#000' : '#888'">{{ btn.label }}</text>
    </g>

    <!-- D-pad -->
    <text :x="RP_X + RP_BTN_W/2" y="252" text-anchor="middle"
          font-family="monospace" font-size="6" fill="#333">D-PAD</text>
    <!-- up -->
    <g @mousedown="noteOn(46)" @mouseup="noteOff(46)" style="cursor:pointer">
      <rect :x="RP_X + 20" y="254" width="18" height="18" rx="2"
            :fill="btnState[46] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.7"/>
      <text :x="RP_X + 29" y="267" text-anchor="middle" font-family="monospace" font-size="9"
            :fill="btnState[46] ? '#000' : '#777'">▲</text>
    </g>
    <!-- left -->
    <g @mousedown="noteOn(44)" @mouseup="noteOff(44)" style="cursor:pointer">
      <rect :x="RP_X" y="274" width="18" height="18" rx="2"
            :fill="btnState[44] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.7"/>
      <text :x="RP_X + 9" y="287" text-anchor="middle" font-family="monospace" font-size="9"
            :fill="btnState[44] ? '#000' : '#777'">◀</text>
    </g>
    <!-- center -->
    <g @mousedown="noteOn(43)" @mouseup="noteOff(43)" style="cursor:pointer">
      <rect :x="RP_X + 20" y="274" width="18" height="18" rx="9"
            :fill="btnState[43] ? '#18c8da' : '#303034'" stroke="#3a3a3c" stroke-width="0.7"/>
      <circle :cx="RP_X + 29" cy="283" r="2.5"
              :fill="btnState[43] ? '#000' : '#666'"/>
    </g>
    <!-- right -->
    <g @mousedown="noteOn(45)" @mouseup="noteOff(45)" style="cursor:pointer">
      <rect :x="RP_X + 40" y="274" width="18" height="18" rx="2"
            :fill="btnState[45] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.7"/>
      <text :x="RP_X + 49" y="287" text-anchor="middle" font-family="monospace" font-size="9"
            :fill="btnState[45] ? '#000' : '#777'">▶</text>
    </g>
    <!-- down -->
    <g @mousedown="noteOn(47)" @mouseup="noteOff(47)" style="cursor:pointer">
      <rect :x="RP_X + 20" y="294" width="18" height="18" rx="2"
            :fill="btnState[47] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.7"/>
      <text :x="RP_X + 29" y="307" text-anchor="middle" font-family="monospace" font-size="9"
            :fill="btnState[47] ? '#000' : '#777'">▼</text>
    </g>

    <!-- Bottom right 2-column grid -->
    <g v-for="(btn, bi) in RP_BTNS_BOT" :key="'rp-bot-'+bi"
       @mousedown="btn.note === 62 ? bankDown() : btn.note === 63 ? bankUp() : noteOn(btn.note)"
       @mouseup="noteOff(btn.note)" style="cursor:pointer">
      <rect
        :x="RP_X + (bi % 2) * 32" :y="318 + Math.floor(bi / 2) * 22"
        width="29" :height="RP_BTN_H" rx="2"
        :fill="btnState[btn.note] ? '#18c8da' : '#242428'" stroke="#3a3a3c" stroke-width="0.7"/>
      <text
        :x="RP_X + (bi % 2) * 32 + 14.5" :y="318 + Math.floor(bi / 2) * 22 + RP_BTN_H/2 + 3.5"
        text-anchor="middle" font-family="monospace" font-size="6"
        :fill="btnState[btn.note] ? '#000' : '#888'">{{ btn.label }}</text>
    </g>

    <!-- ── Decorative section dividers / labels ──────────────────── -->
    <!-- Center section label -->
    <text x="436" y="16" text-anchor="middle"
          font-family="monospace" font-size="7" fill="#333" letter-spacing="2">ABLETON PUSH 3</text>

    <!-- Encoder section bracket -->
    <line x1="152" y1="52" x2="720" y2="52" stroke="#1e1e22" stroke-width="0.5"/>

    <!-- Pad grid label -->
    <text :x="PADS_X" :y="PAD_AREA_Y - 4"
          font-family="monospace" font-size="6" fill="#333">PAD GRID (8×8)</text>

  </svg>
</template>

<style scoped>
.push3-svg {
  display: block;
  width: 100%;
  max-width: 880px;
  height: auto;
  font-family: monospace;
}
</style>
