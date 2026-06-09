<script setup>
import { ref, reactive, computed, watch } from 'vue'

const props = defineProps({
  channels: { type: Array, default: () => [] }
})

const emit = defineEmits(['note-on', 'note-off', 'cc', 'pitch-bend', 'bank-change'])

// ── constants ─────────────────────────────────────────────────────
const W = 600; const H = 310
const SCRIBBLE_H = 22

// Channel x-centers (8 channels + master)
const CH_X = Array.from({ length: 8 }, (_, i) => 30 + i * 50)
const MASTER_X = 440

// Fader geometry
const FADER_TOP    = SCRIBBLE_H + 52
const FADER_BOTTOM = SCRIBBLE_H + 192
const FADER_H      = FADER_BOTTOM - FADER_TOP

// Button rows
const SEL_Y  = SCRIBBLE_H + 202
const MUTE_Y = SCRIBBLE_H + 220
const SOLO_Y = SCRIBBLE_H + 238
const REC_CY = SCRIBBLE_H + 260

// Right panel x-center
const RP_X = 525

// ── state ─────────────────────────────────────────────────────────
const encVals    = reactive(Array(8).fill(64))
const encAngles  = reactive(Array(8).fill(0))
const dragEncIdx = ref(-1)
const jogDragging = ref(false)
const faderVals  = reactive(Array(9).fill(64))
const jogAngle   = ref(0)
const btnState   = reactive({})

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

// Encoder drag — endless: visual angle tracked separately, resets on release
const knobDrag = ref(null)
function startEncDrag(idx, e) {
  e.preventDefault()
  dragEncIdx.value = idx
  knobDrag.value = { idx, isEnc: true, startY: e.clientY, startVal: encVals[idx], startAngle: encAngles[idx] }
  window.addEventListener('mousemove', onKnobMove)
  window.addEventListener('mouseup', onKnobUp)
}
function startJogDrag(e) {
  e.preventDefault()
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
    cc(idx + 16, encVals[idx])
  } else {
    jogAngle.value = ((startAngle + delta * 2) % 360 + 360) % 360
    cc(60, delta > 0 ? 63 : 65)
  }
}
function onKnobUp() {
  dragEncIdx.value = -1
  jogDragging.value = false
  knobDrag.value = null
  window.removeEventListener('mousemove', onKnobMove)
  window.removeEventListener('mouseup', onKnobUp)
}

// Motorfader: update fader positions when channel values arrive
watch(() => props.channels, (channels) => {
  channels.forEach((ch, i) => {
    if (ch?.value != null && dragging.value?.idx !== i) {
      faderVals[i] = Math.round(ch.value * 127)
    }
  })
}, { immediate: true, deep: true })

function bankDown() { emit('bank-change', -1); noteOn(91) }
function bankUp()   { emit('bank-change', +1); noteOn(92) }

// Fader drag
const dragging = ref(null)
function faderDragStart(idx, e) {
  dragging.value = { idx, startY: e.clientY, startVal: faderVals[idx] }
  e.preventDefault()
}
function onMouseMove(e) {
  if (dragging.value === null) return
  const { idx, startY, startVal } = dragging.value
  const dy = startY - e.clientY
  faderVals[idx] = Math.max(0, Math.min(127, startVal + Math.round(dy * 127 / FADER_H)))
  emit('pitch-bend', { ch: idx + 1, val: faderVals[idx] })
}
function onMouseUp() { dragging.value = null }

const chLabels = computed(() =>
  Array.from({ length: 8 }, (_, i) => {
    const ch = props.channels[i]
    return ch?.label || ''
  })
)

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
    <rect x="0" y="0" :width="W" :height="H" rx="6" fill="#111115" stroke="#1e1e22"/>

    <!-- ══ CHANNEL STRIPS (8 + master) ════════════════════════════ -->
    <g v-for="i in 8" :key="'ch'+i">
      <g :transform="`translate(${CH_X[i-1]}, 0)`">

        <!-- scribble strip / inline display -->
        <rect x="-20" y="2" width="40" height="18" rx="2"
              fill="#060812" stroke="#1a2a44" stroke-width="0.8"/>
        <text v-if="chLabels[i-1]" x="0" y="10" text-anchor="middle" font-family="monospace" font-size="7"
              fill="#18c8da" opacity="0.85">{{ chLabels[i-1].slice(0,6) }}</text>
        <text v-if="chLabels[i-1]" x="0" y="19" text-anchor="middle" font-family="monospace" font-size="6"
              fill="#18c8da" opacity="0.35">{{ i }}</text>

        <!-- encoder knob -->
        <g :transform="`translate(0, ${SCRIBBLE_H + 16})`"
           @mousedown="e => startEncDrag(i-1, e)" style="cursor:ns-resize">
          <circle r="13" fill="#1a1a1e" stroke="#484848" stroke-width="1.5"/>
          <circle r="9"  fill="none"   stroke="#2a2a2e" stroke-width="1"/>
          <!-- indicator only visible while dragging this encoder -->
          <line v-if="dragEncIdx === i-1"
                x1="0" y1="-4" x2="0" y2="-11"
                stroke="#18c8da" stroke-width="2" stroke-linecap="round"
                :transform="`rotate(${encAngles[i-1]})`"/>
        </g>

        <!-- LED dot -->
        <circle :cx="0" :cy="SCRIBBLE_H + 34" r="2.5"
                :fill="btnState['encled'+i] ? '#18c8da' : '#222226'"
                :stroke="btnState['encled'+i] ? '#18c8da' : '#2e2e34'"/>

        <!-- fader track -->
        <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3"
              fill="#070709" stroke="#1e1e22"/>
        <!-- fader handle -->
        <rect x="-10" :y="faderY(i-1) - 5" width="20" height="10" rx="2"
              fill="#3a3a3e" stroke="#585860"
              style="cursor:ns-resize"
              @mousedown="faderDragStart(i-1, $event)"/>

        <!-- Sel button -->
        <rect x="-17" :y="SEL_Y" width="34" height="14" rx="2"
              :fill="btnState['sel'+i] ? '#e8889866' : '#c87888aa'" stroke="#aa5566" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('sel'+i, i+23)"/>
        <text x="0" :y="SEL_Y+10" text-anchor="middle" font-family="monospace" font-size="7" font-weight="700"
              :fill="btnState['sel'+i] ? '#fff' : '#dda0a8'">Sel</text>

        <!-- Mute button -->
        <rect x="-17" :y="MUTE_Y" width="34" height="14" rx="2"
              :fill="btnState['mute'+i] ? '#6688bbaa' : '#5577aa66'" stroke="#4466aa" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('mute'+i, i+15)"/>
        <text x="0" :y="MUTE_Y+10" text-anchor="middle" font-family="monospace" font-size="7" font-weight="700"
              :fill="btnState['mute'+i] ? '#fff' : '#88aacc'">Mute</text>

        <!-- Solo button -->
        <rect x="-17" :y="SOLO_Y" width="34" height="14" rx="2"
              :fill="btnState['solo'+i] ? '#66aa88aa' : '#559977aa'" stroke="#448866" stroke-width="0.8"
              style="cursor:pointer" @click="toggle('solo'+i, i+7)"/>
        <text x="0" :y="SOLO_Y+10" text-anchor="middle" font-family="monospace" font-size="7" font-weight="700"
              :fill="btnState['solo'+i] ? '#fff' : '#88ccaa'">Solo</text>

        <!-- Rec circle LED -->
        <circle :cx="0" :cy="REC_CY" r="6"
                :fill="btnState['rec'+i] ? '#ee6633' : '#441e10'"
                :stroke="btnState['rec'+i] ? '#ff8844' : '#663322'"
                style="cursor:pointer"
                @click="toggle('rec'+i, i-1)"/>
      </g>
    </g>

    <!-- master strip -->
    <g :transform="`translate(${MASTER_X}, 0)`">
      <rect x="-20" y="2" width="40" height="18" rx="2"
            fill="#0a0e14" stroke="#1a2a44" stroke-width="0.8"/>
      <text x="0" y="14" text-anchor="middle" font-family="monospace" font-size="9"
            fill="#18c8da" opacity="0.6">M</text>
      <rect x="-3" :y="FADER_TOP" width="6" :height="FADER_H" rx="3" fill="#070709" stroke="#1e1e22"/>
      <rect x="-10" :y="faderY(8) - 5" width="20" height="10" rx="2"
            fill="#3a3a3e" stroke="#585860" style="cursor:ns-resize"
            @mousedown="faderDragStart(8, $event)"/>
    </g>

    <!-- ══ RIGHT PANEL ════════════════════════════════════════════ -->
    <rect x="470" y="0" width="122" :height="H" rx="4" fill="#0d0d10" stroke="#1a1a1e"/>

    <!-- Chan nav -->
    <text :x="RP_X" y="14" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="1">CHAN</text>
    <g v-for="(note, j) in [48, 49]" :key="'chn'+j">
      <rect :x="RP_X - 22 + j*24" y="17" width="20" height="16" rx="2"
            fill="#cc8866cc" stroke="#aa6644" style="cursor:pointer"
            @mousedown="noteOn(note)" @mouseup="noteOff(note)"/>
      <text :x="RP_X - 12 + j*24" y="29" text-anchor="middle" font-family="monospace" font-size="10" fill="#2a1a10">{{ j===0?'‹':'›' }}</text>
    </g>

    <!-- Bank nav -->
    <text :x="RP_X" y="43" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="1">BANK</text>
    <rect :x="RP_X - 22" y="46" width="20" height="16" rx="2"
          fill="#cc8866cc" stroke="#aa6644" style="cursor:pointer" @mousedown="bankDown"/>
    <text :x="RP_X - 12" y="58" text-anchor="middle" font-family="monospace" font-size="10" fill="#2a1a10">«</text>
    <rect :x="RP_X + 2" y="46" width="20" height="16" rx="2"
          fill="#cc8866cc" stroke="#aa6644" style="cursor:pointer" @mousedown="bankUp"/>
    <text :x="RP_X + 12" y="58" text-anchor="middle" font-family="monospace" font-size="10" fill="#2a1a10">»</text>

    <!-- Trans nav -->
    <text :x="RP_X" y="73" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="1">TRANS</text>
    <g v-for="(note, j) in [91, 92]" :key="'trn'+j">
      <rect :x="RP_X - 22 + j*24" y="76" width="20" height="16" rx="2"
            fill="#cc8866cc" stroke="#aa6644" style="cursor:pointer"
            @mousedown="noteOn(note)" @mouseup="noteOff(note)"/>
      <text :x="RP_X - 12 + j*24" y="88" text-anchor="middle" font-family="monospace" font-size="9" fill="#2a1a10">{{ j===0?'⇤':'⇥' }}</text>
    </g>

    <!-- Transport buttons -->
    <g v-for="([label, note, col], j) in [['▷',94,'#338833'],['□',93,'#555'],['●',95,'#883333'],['↩',86,'#555']]" :key="'tr'+j">
      <rect :x="RP_X - 44 + j*22" y="106" width="20" height="18" rx="2"
            :fill="btnState['t'+j] ? col+'dd' : col+'55'" :stroke="col" stroke-width="0.8"
            style="cursor:pointer" @click="toggle('t'+j, note)"/>
      <text :x="RP_X - 34 + j*22" y="119" text-anchor="middle" font-family="monospace" font-size="10" fill="#ccc">{{ label }}</text>
    </g>

    <!-- Mixer / Read / Write -->
    <g v-for="([label, note, col], j) in [['Mixer',84,'#338833'],['Read',74,'#555'],['Write',75,'#555']]" :key="'rw'+j">
      <rect :x="RP_X - 48 + j*32" y="132" width="30" height="14" rx="2"
            :fill="btnState['rw'+j] ? col+'dd' : col+'44'" :stroke="col" stroke-width="0.8"
            style="cursor:pointer" @click="toggle('rw'+j, note)"/>
      <text :x="RP_X - 33 + j*32" y="143" text-anchor="middle" font-family="monospace" font-size="7" fill="#ccc">{{ label }}</text>
    </g>

    <!-- Jog wheel -->
    <text :x="RP_X" y="166" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="1">JOG</text>
    <g :transform="`translate(${RP_X}, 210)`"
       @mousedown="startJogDrag" style="cursor:ns-resize">
      <circle r="36" fill="#0a0a0c" stroke="#3a3a3e" stroke-width="2"/>
      <circle r="28" fill="none" stroke="#1e1e22" stroke-width="1"/>
      <line v-if="jogDragging"
            x1="0" y1="-22" x2="0" y2="-32"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${jogAngle})`"/>
    </g>
  </svg>
</template>
