<script setup>
import { ref, reactive, computed, watch } from 'vue'

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

// ── constants ─────────────────────────────────────────────────────
const W = 600; const H = 310
const SCRIBBLE_H = 22
const CH_X = Array.from({ length: 8 }, (_, i) => 30 + i * 50)
const MASTER_X = 440
const FADER_TOP    = SCRIBBLE_H + 52
const FADER_BOTTOM = SCRIBBLE_H + 192
const FADER_H      = FADER_BOTTOM - FADER_TOP
const SEL_Y  = SCRIBBLE_H + 202
const MUTE_Y = SCRIBBLE_H + 220
const SOLO_Y = SCRIBBLE_H + 238
const REC_CY = SCRIBBLE_H + 260
const RP_X = 525

// ── state ─────────────────────────────────────────────────────────
const encVals    = reactive(Array(8).fill(64))
const encAngles  = reactive(Array(8).fill(0))
const dragEncIdx = ref(-1)
const jogDragging = ref(false)
const faderVals  = reactive(Array(9).fill(64))
const jogAngle   = ref(0)
const btnState   = reactive({})

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
  return props.bindings?.[midiKey(el)]
      || props.bindings?.[`any:${el.channel}:${el.address}`]
      || null
}

function isHighlightedName(name) {
  if (!props.highlighted) return false
  const el = elemFor(name)
  if (!el) return false
  return props.highlighted === midiKey(el)
      || props.highlighted === `any:${el.channel}:${el.address}`
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

function faderY(idx) { return FADER_BOTTOM - (faderVals[idx] / 127) * FADER_H }

// ── encoder drag ──────────────────────────────────────────────────
const knobDrag = ref(null)
function startEncDrag(idx, e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    const name = `knob_${idx + 1}`
    const el = elemFor(name)
    if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: 1, name })
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  dragEncIdx.value = idx
  knobDrag.value = { idx, isEnc: true, startY: e.clientY, startVal: encVals[idx], startAngle: encAngles[idx] }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function startJogDrag(e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    const el = elemFor('jog')
    if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: 1, name: 'jog' })
    else emit('midi-input', { type: '__learn_click__', name: 'jog', value: 1 })
  }
  jogDragging.value = true
  knobDrag.value = { idx: -1, isEnc: false, startY: e.clientY, startAngle: jogAngle.value }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function onKnobMove(e) {
  if (!knobDrag.value) return
  const { idx, isEnc, startY, startVal, startAngle } = knobDrag.value
  const delta = Math.round((startY - e.clientY) * 1.5)
  if (isEnc) {
    encVals[idx] = Math.max(0, Math.min(127, startVal + delta))
    encAngles[idx] = ((startAngle + delta * 4) % 360 + 360) % 360
    const name = `knob_${idx + 1}`
    const el = elemFor(name)
    if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: encVals[idx], name })
  } else {
    jogAngle.value = ((startAngle + delta * 2) % 360 + 360) % 360
    const el = elemFor('jog')
    if (el) emit('midi-input', { type: 'cc', channel: el.channel, address: el.address, value: delta > 0 ? 63 : 65, name: 'jog' })
  }
}
function onKnobUp() {
  dragEncIdx.value = -1
  jogDragging.value = false
  knobDrag.value = null
  window.removeEventListener('mousemove', onKnobMove)
  window.removeEventListener('mouseup', onKnobUp)
}

// ── fader drag ────────────────────────────────────────────────────
const dragging = ref(null)
function faderDragStart(idx, e) {
  e.preventDefault()
  if (props.mode === 'map' || props.mode === 'learn') {
    const name = idx < 8 ? `fader_${idx + 1}` : 'fader_M_video'
    const el = elemFor(name)
    if (el) emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value: 1, name })
    else emit('midi-input', { type: '__learn_click__', name, value: 1 })
  }
  dragging.value = { idx, startY: e.clientY, startVal: faderVals[idx] }
}
function onMouseMove(e) {
  if (dragging.value === null) return
  const { idx, startY, startVal } = dragging.value
  const dy = startY - e.clientY
  faderVals[idx] = Math.max(0, Math.min(127, startVal + Math.round(dy * 127 / FADER_H)))
  const name = idx < 8 ? `fader_${idx + 1}` : 'fader_M_video'
  const el = elemFor(name)
  if (el) emit('midi-input', { type: 'pitch_bend', channel: el.channel, address: 0, value: faderVals[idx], name })
}
function onMouseUp() { dragging.value = null }

// ── bank nav ──────────────────────────────────────────────────────
function bankDown() { emit('bank-change', -1); pressElem('bank_down') }
function bankUp()   { emit('bank-change', +1); pressElem('bank_up') }

// Motorfader
watch(() => props.channels, (channels) => {
  channels.forEach((ch, i) => {
    if (ch?.value != null && dragging.value?.idx !== i)
      faderVals[i] = Math.round(ch.value * 127)
  })
}, { immediate: true, deep: true })

const chLabels = computed(() =>
  Array.from({ length: 8 }, (_, i) => props.channels[i]?.label || '')
)

// ── colour helpers ────────────────────────────────────────────────
function btnFill(name, defaultFill = '#1e1e22') {
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
    <rect x="0" y="0" :width="W" :height="H" rx="6" fill="#111115" stroke="#1e1e22"/>

    <!-- ══ CHANNEL STRIPS (8) ══ -->
    <g v-for="i in 8" :key="'ch'+i" :transform="`translate(${CH_X[i-1]}, 0)`">

      <!-- scribble display -->
      <rect x="-20" y="2" width="40" height="18" rx="2" fill="#060812" stroke="#1a2a44" stroke-width="0.8"/>
      <text v-if="chLabels[i-1]" x="0" y="10" text-anchor="middle" font-family="monospace" font-size="7"
            fill="#18c8da" opacity="0.85">{{ chLabels[i-1].slice(0,6) }}</text>
      <text v-if="chLabels[i-1]" x="0" y="19" text-anchor="middle" font-family="monospace" font-size="6"
            fill="#18c8da" opacity="0.35">{{ i }}</text>

      <!-- encoder (profile: knob_1..8) -->
      <g :transform="`translate(0, ${SCRIBBLE_H + 16})`"
         @mousedown="e => startEncDrag(i-1, e)"
         @mouseenter="hoveredName=`knob_${i}`" @mouseleave="hoveredName=null"
         style="cursor:ns-resize">
        <circle r="13" :fill="bindingForName(`knob_${i}`)?'#1e2428':'#1a1a1e'"
                :stroke="btnStroke(`knob_${i}`,'#484848')" stroke-width="1.5"/>
        <circle r="9"  fill="none" stroke="#2a2a2e" stroke-width="1"/>
        <line v-if="dragEncIdx === i-1" x1="0" y1="-4" x2="0" y2="-11"
              stroke="#18c8da" stroke-width="2" stroke-linecap="round"
              :transform="`rotate(${encAngles[i-1]})`"/>
      </g>

      <!-- LED dot -->
      <circle :cx="0" :cy="SCRIBBLE_H + 34" r="2.5"
              :fill="btnState[`ch_${i}_encled`] ? '#18c8da' : '#222226'"
              :stroke="btnState[`ch_${i}_encled`] ? '#18c8da' : '#2e2e34'"/>

      <!-- fader (profile: fader_1..8) -->
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#070709" stroke="#1e1e22"/>
      <rect x="-10" :y="faderY(i-1) - 5" width="20" height="10" rx="2"
            fill="#3a3a3e" stroke="#585860" style="cursor:ns-resize"
            @mousedown="faderDragStart(i-1, $event)"
            @mouseenter="hoveredName=`fader_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Sel (profile: sel_1..8) -->
      <rect x="-17" :y="SEL_Y" width="34" height="14" rx="2"
            :fill="btnState[`sel_${i}`] ? '#e8889866' : btnFill(`sel_${i}`,'#c87888aa')"
            :stroke="btnStroke(`sel_${i}`,'#aa5566')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`sel_${i}`)"
            @mouseenter="hoveredName=`sel_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Mute (profile: mute_1..8) -->
      <rect x="-17" :y="MUTE_Y" width="34" height="14" rx="2"
            :fill="btnState[`mute_${i}`] ? '#6688bbaa' : btnFill(`mute_${i}`,'#5577aa66')"
            :stroke="btnStroke(`mute_${i}`,'#4466aa')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`mute_${i}`)"
            @mouseenter="hoveredName=`mute_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Solo (profile: solo_1..8) -->
      <rect x="-17" :y="SOLO_Y" width="34" height="14" rx="2"
            :fill="btnState[`solo_${i}`] ? '#66aa88aa' : btnFill(`solo_${i}`,'#559977aa')"
            :stroke="btnStroke(`solo_${i}`,'#448866')" stroke-width="0.8"
            style="cursor:pointer"
            @click="toggleElem(`solo_${i}`)"
            @mouseenter="hoveredName=`solo_${i}`" @mouseleave="hoveredName=null"/>

      <!-- Rec LED (profile: rec_1..8) -->
      <circle :cx="0" :cy="REC_CY" r="6"
              :fill="btnState[`rec_${i}`] ? '#ee6633' : btnFill(`rec_${i}`,'#441e10')"
              :stroke="btnState[`rec_${i}`] ? '#ff8844' : btnStroke(`rec_${i}`,'#663322')"
              style="cursor:pointer"
              @click="toggleElem(`rec_${i}`)"
              @mouseenter="hoveredName=`rec_${i}`" @mouseleave="hoveredName=null"/>
    </g>

    <!-- ══ MASTER STRIP (profile: fader_M_video) ══ -->
    <g :transform="`translate(${MASTER_X}, 0)`">
      <rect x="-20" y="2" width="40" height="18" rx="2" fill="#0a0e14" stroke="#1a2a44" stroke-width="0.8"/>
      <text x="0" y="14" text-anchor="middle" font-family="monospace" font-size="9" fill="#18c8da" opacity="0.6">M</text>
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#070709" stroke="#1e1e22"/>
      <rect x="-10" :y="faderY(8) - 5" width="20" height="10" rx="2"
            fill="#3a3a3e" stroke="#585860" style="cursor:ns-resize"
            @mousedown="faderDragStart(8, $event)"
            @mouseenter="hoveredName='fader_M_video'" @mouseleave="hoveredName=null"/>
    </g>

    <!-- ══ RIGHT PANEL ══ -->
    <rect x="470" y="0" width="122" :height="H" rx="4" fill="#0d0d10" stroke="#1a1a1e"/>

    <!-- Chan nav (profile: chan_down, chan_up) -->
    <g v-for="(name, j) in ['chan_down','chan_up']" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X - 22 + j*24" y="17" width="20" height="16" rx="2"
            :fill="btnFill(name,'#cc8866cc')" :stroke="btnStroke(name,'#aa6644')"/>
    </g>

    <!-- Bank nav (profile: bank_down, bank_up) -->
    <rect :x="RP_X - 22" y="46" width="20" height="16" rx="2"
          :fill="btnFill('bank_down','#cc8866cc')" :stroke="btnStroke('bank_down','#aa6644')"
          style="cursor:pointer"
          @mousedown="bankDown"
          @mouseenter="hoveredName='bank_down'" @mouseleave="hoveredName=null"/>
    <rect :x="RP_X + 2" y="46" width="20" height="16" rx="2"
          :fill="btnFill('bank_up','#cc8866cc')" :stroke="btnStroke('bank_up','#aa6644')"
          style="cursor:pointer"
          @mousedown="bankUp"
          @mouseenter="hoveredName='bank_up'" @mouseleave="hoveredName=null"/>

    <!-- Trans nav (profile: trans_down, trans_up) -->
    <g v-for="(name, j) in ['trans_down','trans_up']" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X - 22 + j*24" y="76" width="20" height="16" rx="2"
            :fill="btnFill(name,'#cc8866cc')" :stroke="btnStroke(name,'#aa6644')"/>
    </g>

    <!-- Transport -->
    <g v-for="(name, j) in ['trans_play','trans_stop','rec','trans_cycle']" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X - 44 + j*22" y="106" width="20" height="18" rx="2"
            :fill="btnFill(name,'#33333355')" :stroke="btnStroke(name,'#555')" stroke-width="0.8"/>
    </g>

    <!-- Mixer / Read / Write -->
    <g v-for="(name, j) in ['mixer','read','write']" :key="name"
       @click="toggleElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RP_X - 48 + j*32" y="132" width="30" height="14" rx="2"
            :fill="btnFill(name,'#33333344')" :stroke="btnStroke(name,'#555')" stroke-width="0.8"/>
    </g>

    <!-- Jog wheel (profile: jog) -->
    <g :transform="`translate(${RP_X}, 210)`"
       @mousedown="startJogDrag"
       @mouseenter="hoveredName='jog'" @mouseleave="hoveredName=null"
       style="cursor:ns-resize">
      <circle r="36" :fill="bindingForName('jog')?'#1e2428':'#0a0a0c'"
              :stroke="btnStroke('jog','#3a3a3e')" stroke-width="2"/>
      <circle r="28" fill="none" stroke="#1e1e22" stroke-width="1"/>
      <line v-if="jogDragging" x1="0" y1="-22" x2="0" y2="-32"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${jogAngle})`"/>
    </g>

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
