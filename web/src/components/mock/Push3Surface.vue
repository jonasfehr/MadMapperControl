<script setup>
import { ref, reactive, computed } from 'vue'

const props = defineProps({
  channels:    { type: Array,  default: () => [] },
  components:  { type: Array,  default: () => [] },
  bindings:    { type: Object, default: () => ({}) },
  mode:        { type: String, default: 'emulator' },
  highlighted: { type: String, default: null },
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

// ── visual state ──────────────────────────────────────────────────
const btnState       = reactive({})
const encAngles      = reactive(Array(8).fill(0))
const dragEncIdx     = ref(-1)
const bigKnobAngles  = reactive({ dial_left_top: 0, dial_left_smal: 0, master_encoder: 0 })
const bigKnobDragKey = ref(null)
const padColors      = ref(Array(64).fill(null))

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
function elemFor(name) {
  if (compMap.value[name]) return compMap.value[name]
  const pm = name?.match(/^pad_(\d+)_(\d+)$/)
  if (pm) return { type: 'note', channel: 1, address: 36 + (parseInt(pm[1]) - 1) * 8 + (parseInt(pm[2]) - 1) }
  return null
}

function midiKey(el) {
  return `${normType(el.type)}:${el.channel}:${el.address}`
}

const tooltipMidi = computed(() => {
  const el = elemFor(hoveredName.value)
  if (!el) return ''
  const t = normType(el.type)
  return t === 'note' ? `Note ${el.address} ch${el.channel}`
       : t === 'cc'   ? `CC ${el.address} ch${el.channel}`
       : `PitchBend ch${el.channel}`
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

// ── MIDI emitters ─────────────────────────────────────────────────
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
function sendNamedCC(name, value) {
  const el = elemFor(name)
  if (!el) return
  emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value, name })
}

// ── encoder / knob drag ───────────────────────────────────────────
const knobDrag = ref(null)
const encVals  = reactive(Array(8).fill(64))
const volVal   = ref(64)
const swingVal = ref(64)
const jogVal   = ref(64)

function startEncDrag(idx, e) {
  e.preventDefault()
  dragEncIdx.value = idx
  const name = `fader_${idx + 1}`
  if (props.mode === 'map' || props.mode === 'learn') {
    if (elemFor(name)) sendNamedCC(name, 1)
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  knobDrag.value = { idx, isEnc: true, startY: e.clientY, startVal: encVals[idx], startAngle: encAngles[idx], name }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function startBigKnobDrag(valRef, name, key, e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    if (elemFor(name)) sendNamedCC(name, 1)
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  bigKnobDragKey.value = key
  knobDrag.value = { valRef, isEnc: false, startY: e.clientY, startVal: valRef.value, startAngle: bigKnobAngles[key], name, key }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function onKnobMove(e) {
  if (!knobDrag.value) return
  const { idx, valRef, isEnc, startY, startVal, startAngle, name, key } = knobDrag.value
  const delta = Math.round((startY - e.clientY) * 1.5)
  if (isEnc) {
    encVals[idx] = Math.max(0, Math.min(127, startVal + delta))
    encAngles[idx] = ((startAngle + delta * 4) % 360 + 360) % 360
    sendNamedCC(name, encVals[idx])
  } else {
    valRef.value = Math.max(0, Math.min(127, startVal + delta))
    bigKnobAngles[key] = ((startAngle + delta * 4) % 360 + 360) % 360
    sendNamedCC(name, valRef.value)
  }
}
function onKnobUp() {
  dragEncIdx.value = -1
  bigKnobDragKey.value = null
  knobDrag.value = null
  window.removeEventListener('mousemove', onKnobMove)
  window.removeEventListener('mouseup', onKnobUp)
}

// ── bank nav ──────────────────────────────────────────────────────
function bankDown() { emit('bank-change', -1); pressElem('arrow_page_left') }
function bankUp()   { emit('bank-change', +1); pressElem('arrow_page_right') }

// ── touch strip ───────────────────────────────────────────────────
const tsActive  = ref(false)
const tsPos     = ref(0)
const tsRectRef = ref(null)
function tsStart(e) {
  tsActive.value = true
  if (props.mode === 'map' || props.mode === 'learn') sendNamedCC('touch_strip', 1)
  tsCompute(e)
  window.addEventListener('mousemove', tsMove)
  window.addEventListener('mouseup', tsEnd)
}
function tsMove(e)  { if (tsActive.value) tsCompute(e) }
function tsEnd()    { tsActive.value = false; window.removeEventListener('mousemove', tsMove); window.removeEventListener('mouseup', tsEnd) }
function tsCompute(e) {
  if (!tsRectRef.value) return
  const r = tsRectRef.value.getBoundingClientRect()
  const y = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height))
  tsPos.value = 1 - y
  sendNamedCC('touch_strip', Math.round((1 - y) * 127))
}

// ── display helpers ───────────────────────────────────────────────
const displaySlots = computed(() => Array.from({ length: 8 }, (_, i) => {
  const ch = props.channels[i]; return ch ? (ch.label || ch.path || '') : ''
}))
function truncate(s, n = 10) { if (!s) return ''; return s.length > n ? s.slice(0, n - 1) + '…' : s }
function barW(val, maxW) { return val == null ? 0 : Math.max(0, Math.min(maxW, val * maxW)) }

// ── pad helpers ───────────────────────────────────────────────────
function padName(i) { return `pad_${Math.floor(i / 8) + 1}_${(i % 8) + 1}` }

// ── expose ────────────────────────────────────────────────────────
defineExpose({
  setPadColor(idx, color) { padColors.value[idx] = color },
  clearPads() { padColors.value = Array(64).fill(null) },
})

// ── SVG layout ────────────────────────────────────────────────────
const ENC_Y = 34; const ENC_R = 11; const ENC_SPACING = 71; const ENC_START_X = 183
const DISP_BTN_Y = 60; const DISP_BTN_H = 16; const DISP_BTN_W = 67; const DISP_START_X = 151
const DISPLAY_X = 151; const DISPLAY_Y = 82; const DISPLAY_W = 568; const DISPLAY_H = 62
const LOWER_BTN_Y = 150
const PAD_W = 67; const PAD_H = 34; const PAD_GAP = 4
const PADS_X = DISPLAY_X; const PADS_Y = 176; const PAD_GRID_H = 8 * (PAD_H + PAD_GAP) - PAD_GAP
const TS_X = DISPLAY_X - 14; const TS_Y = PADS_Y; const TS_W = 10; const TS_H = PAD_GRID_H
const SCENES_X = PADS_X + 8 * (PAD_W + PAD_GAP) - PAD_GAP + 3; const SCENE_BTN_W = 10; const SCENE_BTN_H = PAD_H
const RP_X = 730; const RP_BTN_W = 58; const RP_BTN_H = 18
const LP_X = 16; const LP_BTN_W = 58; const LP_BTN_H = 18

const LP_SMALL_TOP_NAMES = ['set_1',  'set_2',  'set_3',  'set_4']
const LP_SMALL_MID_NAMES = ['lock_1', 'lock_2', 'lock_3', 'lock_4']
const RP_BTNS_TOP_NAMES  = ['device', 'mix', 'session_view', 'clip']
const RP_BTNS_MID_NAMES  = ['swap', 'master_trk']
const RP_BTNS_BOT_NAMES  = [
  'session_mode', 'note_mode', 'scale', 'layout',
  'repeat', 'accent', 'dbl_loop', 'dup_beats',
  'convert', 'delete', 'arrow_octave_up', 'arrow_octave_down',
  'arrow_page_left', 'arrow_page_right', 'shift', 'select',
]
const LP_BTNS_BOT_NAMES = ['tap_tempo', 'metronome', 'quantize', 'fixed_len', 'automate', 'new', 'capture', 'record', 'play']

function padX(i) { return PADS_X + (i % 8) * (PAD_W + PAD_GAP) }
function padY(i) { return PADS_Y + Math.floor(i / 8) * (PAD_H + PAD_GAP) }

// ── unplaced bindings ─────────────────────────────────────────────
const SVG_KEYS = computed(() => {
  const keys = new Set(props.components.map(c => `${normType(c.type)}:${c.channel}:${c.address}`))
  for (let r = 0; r < 8; r++)
    for (let c = 0; c < 8; c++)
      keys.add(`note:1:${36 + r * 8 + c}`)
  return keys
})

const unmatchedBindings = computed(() => {
  const result = []
  const seen = new Set()
  for (const [key, data] of Object.entries(props.bindings || {})) {
    if (key.startsWith('any:')) continue
    if (!SVG_KEYS.value.has(key) && !seen.has(key)) {
      seen.add(key)
      result.push({ ...data, key })
    }
  }
  return result
})

// ── colours / mode ────────────────────────────────────────────────
const isMap = computed(() => props.mode === 'map')

function btnFill(name, defaultFill = '#242428') {
  if (isHighlightedName(name)) return '#243a24'
  if (btnState[name]) return '#18c8da'
  const b = bindingForName(name)
  if (b) return isMap.value ? '#1e2a1e' : '#1e2428'
  return defaultFill
}
function btnStroke(name, defaultStroke = '#3a3a3c') {
  if (isHighlightedName(name)) return '#a0e060'
  if (bindingForName(name)) return '#18c8da44'
  if (isMap.value) return '#444'
  return defaultStroke
}
function padFill(i) {
  if (padColors.value[i]) return padColors.value[i]
  const name = padName(i)
  if (btnState[name]) return '#18c8da'
  if (isHighlightedName(name)) return '#243a24'
  return '#1a1a1e'
}

// ── highlight overlay ─────────────────────────────────────────────
const highlightOverlay = computed(() => {
  if (!props.highlighted) return null
  const [type, , addrStr] = props.highlighted.split(':')
  const address = parseInt(addrStr)
  if (isNaN(address)) return null
  if (type === 'note' && address >= 36 && address <= 99) {
    const i = address - 36
    return { shape: 'rect', x: padX(i) - 2, y: padY(i) - 2, w: PAD_W + 4, h: PAD_H + 4, rx: 4 }
  }
  if (type === 'cc' && address >= 71 && address <= 78) {
    const i = address - 71
    return { shape: 'circle', cx: ENC_START_X + i * ENC_SPACING, cy: ENC_Y, r: ENC_R + 4 }
  }
  return null
})
</script>

<template>
  <svg ref="svgRef" viewBox="0 0 880 560" xmlns="http://www.w3.org/2000/svg" class="push3-svg"
       :style="{ userSelect: 'none' }" @mousemove="onSVGMouseMove">

    <!-- Device body -->
    <rect x="4" y="4" width="872" height="552" rx="20" ry="20" fill="#111113" stroke="#2a2a2e" stroke-width="1.5"/>

    <!-- ═══ LEFT PANEL ═══ -->

    <!-- user -->
    <g @mousedown="pressElem('user')" @mouseup="releaseElem('user')"
       @mouseenter="hoveredName='user'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="20" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('user')" :stroke="btnStroke('user')" stroke-width="0.8"/>
    </g>

    <!-- learn -->
    <g @mousedown="pressElem('learn')" @mouseup="releaseElem('learn')"
       @mouseenter="hoveredName='learn'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="42" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('learn')" :stroke="btnStroke('learn')" stroke-width="0.8"/>
    </g>

    <!-- set_1..4 (small buttons) -->
    <g v-for="(name, si) in LP_SMALL_TOP_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X + si*15" y="76" width="13" height="13" rx="2"
            :fill="btnFill(name,'#1e1e22')" :stroke="btnStroke(name,'#3a3a3c')" stroke-width="0.7"/>
    </g>

    <!-- setup -->
    <g @mousedown="pressElem('setup')" @mouseup="releaseElem('setup')"
       @mouseenter="hoveredName='setup'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="93" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('setup')" :stroke="btnStroke('setup')" stroke-width="0.8"/>
    </g>

    <!-- dial_left_top knob -->
    <g :transform="`translate(${LP_X+29},130)`"
       @mousedown="e=>startBigKnobDrag(volVal,'dial_left_top','dial_left_top',e)"
       @mouseenter="hoveredName='dial_left_top'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="16" :fill="bindingForName('dial_left_top')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.2"/>
      <circle cx="0" cy="0" r="13" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey==='dial_left_top'" x1="0" y1="-5" x2="0" y2="-13"
            stroke="#18c8da" stroke-width="1.8" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.dial_left_top})`"/>
    </g>

    <!-- mute -->
    <g @mousedown="pressElem('mute')" @mouseup="releaseElem('mute')"
       @mouseenter="hoveredName='mute'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="158" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('mute')" :stroke="btnStroke('mute')" stroke-width="0.8"/>
    </g>

    <!-- stop_clip -->
    <g @mousedown="pressElem('stop_clip')" @mouseup="releaseElem('stop_clip')"
       @mouseenter="hoveredName='stop_clip'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="180" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('stop_clip')" :stroke="btnStroke('stop_clip')" stroke-width="0.8"/>
    </g>

    <!-- lock_1..4 (small buttons) -->
    <g v-for="(name, si) in LP_SMALL_MID_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X + si*15" y="213" width="13" height="13" rx="2"
            :fill="btnFill(name,'#1e1e22')" :stroke="btnStroke(name,'#3a3a3c')" stroke-width="0.7"/>
    </g>

    <!-- solo -->
    <g @mousedown="pressElem('solo')" @mouseup="releaseElem('solo')"
       @mouseenter="hoveredName='solo'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" y="230" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('solo')" :stroke="btnStroke('solo')" stroke-width="0.8"/>
    </g>

    <!-- dial_left_smal knob -->
    <g :transform="`translate(${LP_X+29},270)`"
       @mousedown="e=>startBigKnobDrag(swingVal,'dial_left_smal','dial_left_smal',e)"
       @mouseenter="hoveredName='dial_left_smal'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="16" :fill="bindingForName('dial_left_smal')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.2"/>
      <circle cx="0" cy="0" r="13" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey==='dial_left_smal'" x1="0" y1="-5" x2="0" y2="-13"
            stroke="#18c8da" stroke-width="1.8" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.dial_left_smal})`"/>
    </g>

    <!-- LP bottom buttons -->
    <g v-for="(name, bi) in LP_BTNS_BOT_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="LP_X" :y="300+bi*22" :width="LP_BTN_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill(name)" :stroke="btnStroke(name)" stroke-width="0.8"/>
    </g>

    <!-- ═══ CENTER PANEL ═══ -->

    <!-- 8 Encoders (profile: fader_1..8) -->
    <g v-for="i in 8" :key="'enc-'+i"
       :transform="`translate(${ENC_START_X+(i-1)*ENC_SPACING},${ENC_Y})`"
       @mousedown="e=>startEncDrag(i-1,e)"
       @mouseenter="hoveredName=`fader_${i}`" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle cx="0" cy="0" :r="ENC_R" :fill="btnFill(`fader_${i}`,'#1e1e20')" stroke="#484848" stroke-width="1"/>
      <circle cx="0" cy="0" :r="ENC_R-3" fill="none" stroke="#2e2e32" stroke-width="0.6"/>
      <line v-if="dragEncIdx===i-1" x1="0" y1="-3" :x2="0" :y2="-(ENC_R-2)"
            stroke="#18c8da" stroke-width="1.5" stroke-linecap="round"
            :transform="`rotate(${encAngles[i-1]})`"/>
      <text x="0" :y="ENC_R+9" text-anchor="middle" font-family="monospace" font-size="6" fill="#555">{{ truncate(displaySlots[i-1],7) }}</text>
    </g>

    <!-- Upper display buttons (profile: subpage_1..8) -->
    <g v-for="i in 8" :key="'udb-'+i"
       @mousedown="pressElem(`subpage_${i}`,127)" @mouseup="releaseElem(`subpage_${i}`)"
       @mouseenter="hoveredName=`subpage_${i}`" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="DISP_START_X+(i-1)*(DISP_BTN_W+4)" :y="DISP_BTN_Y" :width="DISP_BTN_W" :height="DISP_BTN_H" rx="2"
            :fill="btnFill(`subpage_${i}`,'#1e1e22')" :stroke="btnStroke(`subpage_${i}`,'#353538')" stroke-width="0.8"/>
      <text :x="DISP_START_X+(i-1)*(DISP_BTN_W+4)+DISP_BTN_W/2" :y="DISP_BTN_Y+DISP_BTN_H/2+3"
            text-anchor="middle" font-family="monospace" font-size="6" fill="#555">{{ truncate(displaySlots[i-1],8) }}</text>
    </g>

    <!-- Main display -->
    <rect :x="DISPLAY_X" :y="DISPLAY_Y" :width="DISPLAY_W" :height="DISPLAY_H" rx="3" fill="#050508" stroke="#1e1e22" stroke-width="1"/>
    <g v-for="i in 8" :key="'dsp-'+i">
      <text v-if="displaySlots[i-1]" :x="DISPLAY_X+10+(i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y+18"
            font-family="monospace" font-size="7" fill="#18c8da" opacity="0.7">{{ truncate(displaySlots[i-1],9) }}</text>
      <template v-if="channels[i-1]">
        <rect :x="DISPLAY_X+4+(i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y+28" :width="DISPLAY_W/8-8" height="8" rx="1.5" fill="#0a0f12"/>
        <rect v-if="channels[i-1].value!=null"
              :x="DISPLAY_X+4+(i-1)*(DISPLAY_W/8)" :y="DISPLAY_Y+28"
              :width="barW(channels[i-1].value,DISPLAY_W/8-8)" height="8" rx="1.5" fill="#18c8da" opacity="0.65"/>
      </template>
      <line v-if="i<8" :x1="DISPLAY_X+i*(DISPLAY_W/8)" :y1="DISPLAY_Y+4"
            :x2="DISPLAY_X+i*(DISPLAY_W/8)" :y2="DISPLAY_Y+DISPLAY_H-4" stroke="#1e1e26" stroke-width="0.5"/>
    </g>
    <text :x="DISPLAY_X+DISPLAY_W/2" :y="DISPLAY_Y+DISPLAY_H-8" text-anchor="middle"
          font-family="monospace" font-size="7" fill="#18c8da" opacity="0.2" letter-spacing="3">PUSH 3</text>

    <!-- Lower display buttons (profile: media_1..8) -->
    <g v-for="i in 8" :key="'ldb-'+i"
       @mousedown="pressElem(`media_${i}`,127)" @mouseup="releaseElem(`media_${i}`)"
       @mouseenter="hoveredName=`media_${i}`" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="DISP_START_X+(i-1)*(DISP_BTN_W+4)" :y="LOWER_BTN_Y" :width="DISP_BTN_W" :height="DISP_BTN_H" rx="2"
            :fill="btnFill(`media_${i}`,'#1e1e22')" :stroke="btnStroke(`media_${i}`,'#353538')" stroke-width="0.8"/>
    </g>

    <!-- Touch strip -->
    <rect ref="tsRectRef" :x="TS_X" :y="TS_Y" :width="TS_W" :height="TS_H" rx="4"
          fill="#0a0a0e" stroke="#222" stroke-width="0.8" style="cursor:ns-resize"
          @mousedown.prevent="tsStart"
          @mouseenter="hoveredName='touch_strip'" @mouseleave="hoveredName=null"/>
    <rect :x="TS_X+2" :y="TS_Y+2+(1-tsPos)*(TS_H-4)" :width="TS_W-4" :height="tsPos*(TS_H-4)" rx="2"
          fill="#18c8da" opacity="0.4" pointer-events="none"/>

    <!-- Pad grid -->
    <g v-for="i in 64" :key="'pad-'+i"
       @mousedown="pressElem(padName(i-1))"
       @mouseup="releaseElem(padName(i-1))"
       @mouseenter="hoveredName=padName(i-1)" @mouseleave="hoveredName=null"
       style="cursor:pointer">
      <rect :x="padX(i-1)" :y="padY(i-1)" :width="PAD_W" :height="PAD_H" rx="2"
            :fill="padFill(i-1)"
            :stroke="padColors[i-1]?'none':btnStroke(padName(i-1),'#2a2a2e')" stroke-width="0.8"/>
    </g>

    <!-- Scene launch -->
    <g v-for="i in 8" :key="'scene-'+i"
       @mousedown="pressElem(`scene_${i}`)" @mouseup="releaseElem(`scene_${i}`)"
       @mouseenter="hoveredName=`scene_${i}`" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="SCENES_X" :y="PADS_Y+(i-1)*(SCENE_BTN_H+PAD_GAP)" :width="SCENE_BTN_W" :height="SCENE_BTN_H" rx="2"
            :fill="btnFill(`scene_${i}`,'#1e1e22')" :stroke="btnStroke(`scene_${i}`,'#2a2a2e')" stroke-width="0.8"/>
    </g>

    <!-- ═══ RIGHT PANEL ═══ -->

    <!-- RP top buttons -->
    <g v-for="(name, bi) in RP_BTNS_TOP_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X" :y="20+bi*22" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            :fill="btnFill(name)" :stroke="btnStroke(name)" stroke-width="0.8"/>
    </g>

    <!-- add -->
    <g @mousedown="pressElem('add')" @mouseup="releaseElem('add')"
       @mouseenter="hoveredName='add'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X" y="110" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            :fill="btnFill('add','#1e3a3e')" stroke="#18c8da" stroke-width="0.8"/>
    </g>

    <!-- master_encoder (jog wheel) -->
    <g :transform="`translate(${RP_X+34},163)`"
       @mousedown="e=>startBigKnobDrag(jogVal,'master_encoder','master_encoder',e)"
       @mouseenter="hoveredName='master_encoder'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" r="22" :fill="bindingForName('master_encoder')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.5"/>
      <circle cx="0" cy="0" r="18" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <circle cx="0" cy="0" r="8"  fill="#181818" stroke="#3a3a3c" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey==='master_encoder'" x1="0" y1="-6" x2="0" y2="-17"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.master_encoder})`"/>
    </g>

    <!-- RP mid buttons -->
    <g v-for="(name, bi) in RP_BTNS_MID_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X" :y="198+bi*22" :width="RP_BTN_W" :height="RP_BTN_H" rx="2"
            :fill="btnFill(name)" :stroke="btnStroke(name)" stroke-width="0.8"/>
    </g>

    <!-- D-pad -->
    <g @mousedown="pressElem('dpad_up')" @mouseup="releaseElem('dpad_up')"
       @mouseenter="hoveredName='dpad_up'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X+20" y="254" width="18" height="18" rx="2"
            :fill="btnFill('dpad_up')" :stroke="btnStroke('dpad_up')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_left')" @mouseup="releaseElem('dpad_left')"
       @mouseenter="hoveredName='dpad_left'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X" y="274" width="18" height="18" rx="2"
            :fill="btnFill('dpad_left')" :stroke="btnStroke('dpad_left')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_center')" @mouseup="releaseElem('dpad_center')"
       @mouseenter="hoveredName='dpad_center'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X+20" y="274" width="18" height="18" rx="9"
            :fill="btnFill('dpad_center','#303034')" :stroke="btnStroke('dpad_center')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_right')" @mouseup="releaseElem('dpad_right')"
       @mouseenter="hoveredName='dpad_right'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X+40" y="274" width="18" height="18" rx="2"
            :fill="btnFill('dpad_right')" :stroke="btnStroke('dpad_right')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_down')" @mouseup="releaseElem('dpad_down')"
       @mouseenter="hoveredName='dpad_down'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X+20" y="294" width="18" height="18" rx="2"
            :fill="btnFill('dpad_down')" :stroke="btnStroke('dpad_down')" stroke-width="0.7"/>
    </g>

    <!-- RP bottom 2-column grid -->
    <g v-for="(name, bi) in RP_BTNS_BOT_NAMES" :key="name"
       @mousedown="name==='arrow_page_left'?bankDown():name==='arrow_page_right'?bankUp():pressElem(name)"
       @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X+(bi%2)*32" :y="318+Math.floor(bi/2)*22" width="29" :height="RP_BTN_H" rx="2"
            :fill="btnFill(name)" :stroke="btnStroke(name)" stroke-width="0.7"/>
    </g>

    <!-- Decorative -->
    <text x="436" y="16" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="2">ABLETON PUSH 3</text>
    <line x1="152" y1="52" x2="720" y2="52" stroke="#1e1e22" stroke-width="0.5"/>

    <!-- Map mode hint -->
    <text v-if="isMap" x="436" y="556" text-anchor="middle" font-family="monospace" font-size="7" fill="#18c8da44">MAP MODE — click any element to assign a role</text>

    <!-- Unplaced bindings -->
    <template v-if="unmatchedBindings.length">
      <text x="152" y="547" font-family="monospace" font-size="6" fill="#555">UNPLACED</text>
      <g v-for="(b, bi) in unmatchedBindings" :key="b.key">
        <rect :x="152 + bi * 88" y="550" width="84" height="14" rx="2"
              :fill="props.highlighted === b.key ? '#243a24' : '#1e1e22'"
              :stroke="props.highlighted === b.key ? '#a0e060' : '#444'"
              :stroke-width="props.highlighted === b.key ? '2' : '0.8'"/>
        <text :x="152 + bi * 88 + 42" y="559" text-anchor="middle" font-family="monospace" font-size="6.5"
              :fill="props.highlighted === b.key ? '#a0e060' : '#888'">{{ b.label }}</text>
      </g>
    </template>

    <!-- Component highlight overlay -->
    <template v-if="highlightOverlay">
      <rect v-if="highlightOverlay.shape==='rect'"
            :x="highlightOverlay.x" :y="highlightOverlay.y"
            :width="highlightOverlay.w" :height="highlightOverlay.h" :rx="highlightOverlay.rx"
            fill="none" stroke="#a0e060" stroke-width="2.5" pointer-events="none"/>
      <circle v-else-if="highlightOverlay.shape==='circle'"
              :cx="highlightOverlay.cx" :cy="highlightOverlay.cy" :r="highlightOverlay.r"
              fill="none" stroke="#a0e060" stroke-width="2.5" pointer-events="none"/>
    </template>

    <!-- Hover tooltip (rendered last so it's on top) -->
    <g v-if="hoveredName"
       :transform="`translate(${Math.min(tooltipPos.x + 10, 730)}, ${Math.min(tooltipPos.y + 10, 520)})`"
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

<style scoped>
.push3-svg { display: block; width: 100%; max-width: 880px; height: auto; }
</style>
