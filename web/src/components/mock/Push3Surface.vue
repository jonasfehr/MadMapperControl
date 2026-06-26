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

const btnState       = reactive({})
const encAngles      = reactive(Array(8).fill(0))
const dragEncIdx     = ref(-1)
const bigKnobAngles  = reactive({ volume: 0, tempo: 0, jog: 0 })
const bigKnobDragKey = ref(null)
const padColors      = ref(Array(64).fill(null))

const svgRef      = ref(null)
const hoveredName = ref(null)
const tooltipPos  = reactive({ x: 0, y: 0 })

function onSVGMouseMove(e) {
  const svg = svgRef.value; if (!svg || !hoveredName.value) return
  const pt = svg.createSVGPoint(); pt.x = e.clientX; pt.y = e.clientY
  const p = pt.matrixTransform(svg.getScreenCTM().inverse())
  tooltipPos.x = p.x; tooltipPos.y = p.y
}

function elemFor(name) {
  if (compMap.value[name]) return compMap.value[name]
  const pm = name?.match(/^pad_(\d+)_(\d+)$/)
  if (pm) return { type: 'note', channel: 1, address: 36 + (parseInt(pm[1])-1)*8 + (parseInt(pm[2])-1) }
  return null
}
function midiKey(el) { return `${normType(el.type)}:${el.channel}:${el.address}` }

const tooltipMidi = computed(() => {
  const el = elemFor(hoveredName.value); if (!el) return ''
  const t = normType(el.type)
  return t==='note' ? `Note ${el.address} ch${el.channel}` : t==='cc' ? `CC ${el.address} ch${el.channel}` : `PitchBend ch${el.channel}`
})
const tooltipRole = computed(() => {
  const el = elemFor(hoveredName.value); if (!el) return '—'
  return props.bindings?.[midiKey(el)]?.role || '—'
})
function bindingForName(name) {
  const el = elemFor(name); if (!el) return null
  return props.bindings?.[midiKey(el)] || null
}
function isHighlightedName(name) {
  if (!props.highlighted) return false
  const el = elemFor(name); if (!el) return false
  return props.highlighted === midiKey(el)
}

function pressElem(name, value=127) {
  const el = elemFor(name)
  if (!el) { if (props.mode==='map'||props.mode==='learn') emit('midi-input',{type:'__learn_click__',name,value:1}); return }
  btnState[name] = true
  emit('midi-input',{type:normType(el.type),channel:el.channel,address:el.address,value,name})
}
function releaseElem(name) {
  const el = elemFor(name); if (!el) return
  btnState[name] = false
  emit('midi-input',{type:normType(el.type),channel:el.channel,address:el.address,value:0,name})
}
function sendNamedCC(name, value) {
  const el = elemFor(name); if (!el) return
  emit('midi-input',{type:'cc',channel:el.channel,address:el.address,value,name})
}

const knobDrag = ref(null)
const encVals  = reactive(Array(8).fill(64))
const volVal   = ref(64); const swingVal = ref(64); const jogVal = ref(64)

function startEncDrag(idx, e) {
  e.preventDefault(); dragEncIdx.value = idx
  const name = `fader_${idx+1}`
  if (props.mode==='map'||props.mode==='learn') { if (elemFor(name)) sendNamedCC(name,1); else emit('midi-input',{type:'__learn_click__',name,value:1}) }
  knobDrag.value = {idx,isEnc:true,startY:e.clientY,startVal:encVals[idx],startAngle:encAngles[idx],name}
  window.addEventListener('mousemove',onKnobMove); window.addEventListener('mouseup',onKnobUp)
}
function startBigKnobDrag(valRef, name, key, e) {
  e.preventDefault()
  if (props.mode==='map'||props.mode==='learn') { if (elemFor(name)) sendNamedCC(name,1); else emit('midi-input',{type:'__learn_click__',name,value:1}) }
  bigKnobDragKey.value = key
  knobDrag.value = {valRef,isEnc:false,startY:e.clientY,startVal:valRef.value,startAngle:bigKnobAngles[key],name,key}
  window.addEventListener('mousemove',onKnobMove); window.addEventListener('mouseup',onKnobUp)
}
function onKnobMove(e) {
  if (!knobDrag.value) return
  const {idx,valRef,isEnc,startY,startVal,startAngle,name,key} = knobDrag.value
  const delta = Math.round((startY-e.clientY)*1.5)
  if (isEnc) { encVals[idx]=Math.max(0,Math.min(127,startVal+delta)); encAngles[idx]=((startAngle+delta*4)%360+360)%360; sendNamedCC(name,encVals[idx]) }
  else { valRef.value=Math.max(0,Math.min(127,startVal+delta)); bigKnobAngles[key]=((startAngle+delta*4)%360+360)%360; sendNamedCC(name,valRef.value) }
}
function onKnobUp() {
  dragEncIdx.value=-1; bigKnobDragKey.value=null; knobDrag.value=null
  window.removeEventListener('mousemove',onKnobMove); window.removeEventListener('mouseup',onKnobUp)
}

function bankDown() { emit('bank-change',-1); pressElem('arrow_page_left') }
function bankUp()   { emit('bank-change',+1); pressElem('arrow_page_right') }

const tsActive=ref(false); const tsPos=ref(0); const tsRectRef=ref(null)
function tsStart(e) { tsActive.value=true; if(props.mode==='map'||props.mode==='learn') sendNamedCC('touch_strip',1); tsCompute(e); window.addEventListener('mousemove',tsMove); window.addEventListener('mouseup',tsEnd) }
function tsMove(e) { if(tsActive.value) tsCompute(e) }
function tsEnd() { tsActive.value=false; window.removeEventListener('mousemove',tsMove); window.removeEventListener('mouseup',tsEnd) }
function tsCompute(e) { if(!tsRectRef.value) return; const r=tsRectRef.value.getBoundingClientRect(); const y=Math.max(0,Math.min(1,(e.clientY-r.top)/r.height)); tsPos.value=1-y; sendNamedCC('touch_strip',Math.round((1-y)*127)) }

const displaySlots = computed(()=>Array.from({length:8},(_,i)=>{ const ch=props.channels[i]; return ch?(ch.label||ch.path||''):'' }))
function truncate(s,n=10) { if(!s) return ''; return s.length>n?s.slice(0,n-1)+'…':s }
function barW(val,maxW) { return val==null?0:Math.max(0,Math.min(maxW,val*maxW)) }
function padName(i) { return `pad_${Math.floor(i/8)+1}_${(i%8)+1}` }

defineExpose({
  setPadColor(idx,color){padColors.value[idx]=color},
  clearPads(){padColors.value=Array(64).fill(null)},
})

// ── Layout constants ──────────────────────────────────────────────
const ENC_Y=34; const ENC_R=11; const ENC_SPACING=71; const ENC_START_X=183
const DISP_BTN_Y=60; const DISP_BTN_H=16; const DISP_BTN_W=67; const DISP_START_X=151
const DISPLAY_X=151; const DISPLAY_Y=82; const DISPLAY_W=568; const DISPLAY_H=62
const LOWER_BTN_Y=150
const PAD_W=67; const PAD_H=34; const PAD_GAP=4
const PADS_X=DISPLAY_X; const PADS_Y=176; const PAD_GRID_H=8*(PAD_H+PAD_GAP)-PAD_GAP
const TS_X=76; const TS_Y=PADS_Y; const TS_W=RC_W; const TS_H=PAD_GRID_H  // aligned with undo/save, same width
const SCENE_BTN_H=PAD_H
const LP_BTN_H=18; const SMALL_BTN=14; const SMALL_PITCH=16
// Right column: add, swap, master_trk, scene_1..8 all share same x and width
const RC_X=DISPLAY_X+DISPLAY_W+3; const RC_W=20
// Knob centers — volume & jog vertically centered with display (y=82+31=113)
const VOL_X=44;  const VOL_Y=DISPLAY_Y+Math.round(DISPLAY_H/2);  const VOL_R=24
const JOG_X=825; const JOG_Y=DISPLAY_Y+Math.round(DISPLAY_H/2); const JOG_R=35
const TEMPO_X=44; const TEMPO_Y=PADS_Y+30; const TEMPO_R=18

const LP_SMALL_TOP_NAMES = ['sets','setup','learn','user']
const LP_SMALL_MID_NAMES = ['lock','stop_clip','mute','solo']
const RP_SMALL_TOP_NAMES = ['device','mix','clip','session_view']
const LP_BTNS_BOT_NAMES  = ['tap_tempo','metronome','quantize','fixed_len','automate','new','capture','record','play']

function padX(i) { return PADS_X+(i%8)*(PAD_W+PAD_GAP) }
function padY(i) { return PADS_Y+Math.floor(i/8)*(PAD_H+PAD_GAP) }

const SVG_KEYS = computed(()=>{
  const keys = new Set(props.components.map(c=>`${normType(c.type)}:${c.channel}:${c.address}`))
  for(let r=0;r<8;r++) for(let c=0;c<8;c++) keys.add(`note:1:${36+r*8+c}`)
  return keys
})
const unmatchedBindings = computed(()=>{
  const result=[]; const seen=new Set()
  for(const [key,data] of Object.entries(props.bindings||{})) {
    if(key.startsWith('any:')) continue
    if(!SVG_KEYS.value.has(key)&&!seen.has(key)) { seen.add(key); result.push({...data,key}) }
  }
  return result
})

const isMap = computed(()=>props.mode==='map')

function btnFill(name, def='#242428') {
  if(isHighlightedName(name)) return '#243a24'
  if(btnState[name]) return '#18c8da'
  if(bindingForName(name)) return isMap.value?'#1e2a1e':'#1e2428'
  return def
}
function btnStroke(name, def='#3a3a3c') {
  if(isHighlightedName(name)) return '#a0e060'
  if(bindingForName(name)) return '#18c8da44'
  if(isMap.value) return '#444'
  return def
}
function padFill(i) {
  if(padColors.value[i]) return padColors.value[i]
  const name=padName(i)
  if(btnState[name]) return '#18c8da'
  if(isHighlightedName(name)) return '#243a24'
  return '#1a1a1e'
}


const highlightOverlay = computed(()=>{
  if(!props.highlighted) return null
  const [type,,addrStr]=props.highlighted.split(':'); const address=parseInt(addrStr); if(isNaN(address)) return null
  if(type==='note'&&address>=36&&address<=99) { const i=address-36; return {shape:'rect',x:padX(i)-2,y:padY(i)-2,w:PAD_W+4,h:PAD_H+4,rx:4} }
  if(type==='cc'&&address>=71&&address<=78) { const i=address-71; return {shape:'circle',cx:ENC_START_X+i*ENC_SPACING,cy:ENC_Y,r:ENC_R+4} }
  return null
})
</script>

<template>
  <svg ref="svgRef" viewBox="0 0 880 560" xmlns="http://www.w3.org/2000/svg" class="push3-svg"
       :style="{userSelect:'none'}" @mousemove="onSVGMouseMove">

    <!-- ═══ ICON SYMBOLS ═══ -->
    <defs>
      <!-- page with lines -->
      <symbol id="ico-sets" viewBox="0 0 10 10">
        <rect x="1.5" y="0.5" width="7" height="9" rx="0.5" fill="none" stroke="currentColor" stroke-width="0.9"/>
        <line x1="3" y1="3.5" x2="7.5" y2="3.5" stroke="currentColor" stroke-width="0.7" stroke-linecap="round"/>
        <line x1="3" y1="5.5" x2="7.5" y2="5.5" stroke="currentColor" stroke-width="0.7" stroke-linecap="round"/>
        <line x1="3" y1="7.5" x2="6.5" y2="7.5" stroke="currentColor" stroke-width="0.7" stroke-linecap="round"/>
      </symbol>
      <!-- cogwheel -->
      <symbol id="ico-setup" viewBox="0 0 10 10">
        <circle cx="5" cy="5" r="2" fill="none" stroke="currentColor" stroke-width="0.8"/>
        <line x1="5" y1="0.2" x2="5" y2="2.2" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="5" y1="7.8" x2="5" y2="9.8" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="0.2" y1="5" x2="2.2" y2="5" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="7.8" y1="5" x2="9.8" y2="5" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="1.5" y1="1.5" x2="2.9" y2="2.9" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="7.1" y1="7.1" x2="8.5" y2="8.5" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="8.5" y1="1.5" x2="7.1" y2="2.9" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
        <line x1="2.9" y1="7.1" x2="1.5" y2="8.5" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>
      </symbol>
      <!-- lightbulb -->
      <symbol id="ico-learn" viewBox="0 0 10 10">
        <path d="M3 4.5 A2.2 2.2 0 1 1 7 4.5 L6.5 7.2 H3.5 Z" fill="none" stroke="currentColor" stroke-width="0.85" stroke-linejoin="round"/>
        <line x1="3.5" y1="7.8" x2="6.5" y2="7.8" stroke="currentColor" stroke-width="0.7"/>
        <line x1="4"   y1="8.8" x2="6"   y2="8.8" stroke="currentColor" stroke-width="0.7"/>
      </symbol>
      <!-- head + upper body -->
      <symbol id="ico-user" viewBox="0 0 10 10">
        <circle cx="5" cy="3" r="2.1" fill="none" stroke="currentColor" stroke-width="0.85"/>
        <path d="M1 9.5 Q1 6 5 6 Q9 6 9 9.5" fill="none" stroke="currentColor" stroke-width="0.85"/>
      </symbol>
      <!-- padlock -->
      <symbol id="ico-lock" viewBox="0 0 10 10">
        <path d="M3.2 4.5 V2.8 A1.8 1.8 0 0 1 6.8 2.8 V4.5" fill="none" stroke="currentColor" stroke-width="0.9"/>
        <rect x="1.5" y="4.5" width="7" height="5" rx="1" fill="none" stroke="currentColor" stroke-width="0.9"/>
        <circle cx="5" cy="7" r="0.9" fill="currentColor"/>
      </symbol>
      <!-- empty square -->
      <symbol id="ico-stop_clip" viewBox="0 0 10 10">
        <rect x="1.5" y="1.5" width="7" height="7" fill="none" stroke="currentColor" stroke-width="1.1"/>
      </symbol>
      <!-- M (mute) -->
      <symbol id="ico-mute" viewBox="0 0 10 10">
        <text x="5" y="8" text-anchor="middle" font-family="sans-serif" font-size="8" font-weight="700" fill="currentColor">M</text>
      </symbol>
      <!-- S (solo) -->
      <symbol id="ico-solo" viewBox="0 0 10 10">
        <text x="5" y="8" text-anchor="middle" font-family="sans-serif" font-size="8" font-weight="700" fill="currentColor">S</text>
      </symbol>
      <!-- open circle (device) -->
      <symbol id="ico-device" viewBox="0 0 10 10">
        <circle cx="5" cy="5" r="3.8" fill="none" stroke="currentColor" stroke-width="1.1"/>
      </symbol>
      <!-- 4 vertical bars, bottom-aligned, different heights (mix) -->
      <symbol id="ico-mix" viewBox="0 0 10 10">
        <rect x="0.5" y="5"   width="1.5" height="4.5" rx="0.3" fill="currentColor"/>
        <rect x="2.5" y="2.5" width="1.5" height="7"   rx="0.3" fill="currentColor"/>
        <rect x="5"   y="0.5" width="1.5" height="9"   rx="0.3" fill="currentColor"/>
        <rect x="7.5" y="3.5" width="1.5" height="6"   rx="0.3" fill="currentColor"/>
      </symbol>
      <!-- rectangle with center dot (clip) -->
      <symbol id="ico-clip" viewBox="0 0 10 10">
        <rect x="1" y="2" width="8" height="6" rx="0.5" fill="none" stroke="currentColor" stroke-width="0.9"/>
        <circle cx="5" cy="5" r="1.1" fill="currentColor"/>
      </symbol>
      <!-- 3 equal vertical bars (session_view) -->
      <symbol id="ico-session_view" viewBox="0 0 10 10">
        <rect x="1"   y="1.5" width="2" height="7" rx="0.3" fill="currentColor"/>
        <rect x="4"   y="1.5" width="2" height="7" rx="0.3" fill="currentColor"/>
        <rect x="7"   y="1.5" width="2" height="7" rx="0.3" fill="currentColor"/>
      </symbol>
      <!-- two circling arrows (swap) -->
      <symbol id="ico-swap" viewBox="0 0 10 10">
        <path d="M1.5 5 A3.5 3.5 0 0 1 8.5 5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linecap="round"/>
        <polyline points="7,2.5 8.5,5 6,5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linejoin="round" stroke-linecap="round"/>
        <path d="M8.5 5 A3.5 3.5 0 0 1 1.5 5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linecap="round"/>
        <polyline points="3,7.5 1.5,5 4,5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linejoin="round" stroke-linecap="round"/>
      </symbol>
      <!-- undo arrow -->
      <symbol id="ico-undo" viewBox="0 0 10 10">
        <path d="M8.5 7 A4 4 0 0 0 3 2.5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linecap="round"/>
        <polyline points="1,2.5 3,2.5 3,4.5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linejoin="round" stroke-linecap="round"/>
      </symbol>
      <!-- save / down-into-tray -->
      <symbol id="ico-save" viewBox="0 0 10 10">
        <line x1="5" y1="1" x2="5" y2="7" stroke="currentColor" stroke-width="0.9" stroke-linecap="round"/>
        <polyline points="3,5 5,7.5 7,5" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linejoin="round" stroke-linecap="round"/>
        <line x1="1.5" y1="9" x2="8.5" y2="9" stroke="currentColor" stroke-width="0.9" stroke-linecap="round"/>
      </symbol>
    </defs>

    <!-- Device body -->
    <rect x="4" y="4" width="872" height="552" rx="20" ry="20" fill="#111113" stroke="#2a2a2e" stroke-width="1.5"/>

    <!-- ═══ ENCODERS ═══ -->
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

    <!-- ═══ LEFT PANEL ═══ -->

    <!-- sets / setup / learn / user  — same row as subpage buttons -->
    <g v-for="(name,si) in LP_SMALL_TOP_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="12+si*SMALL_PITCH" :y="DISP_BTN_Y" :width="SMALL_BTN" :height="SMALL_BTN" rx="2"
            :fill="btnFill(name,'#1e1e22')" :stroke="btnStroke(name,'#3a3a3c')" stroke-width="0.7"/>
      <use :href="'#ico-'+name" :x="12+si*SMALL_PITCH+2" :y="DISP_BTN_Y+2" width="10" height="10"
           color="#777" pointer-events="none"/>
    </g>

    <!-- Volume knob -->
    <g :transform="`translate(${VOL_X},${VOL_Y})`"
       @mousedown="e=>startBigKnobDrag(volVal,'volume','volume',e)"
       @mouseenter="hoveredName='volume'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" :r="VOL_R" :fill="bindingForName('volume')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.5"/>
      <circle cx="0" cy="0" :r="VOL_R-4" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey==='volume'" x1="0" y1="-6" x2="0" y2="-19"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.volume})`"/>
    </g>

    <!-- Undo — top-left of display, same column & width as touch_strip -->
    <g @mousedown="pressElem('undo')" @mouseup="releaseElem('undo')"
       @mouseenter="hoveredName='undo'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="TS_X" :y="DISPLAY_Y" :width="RC_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('undo')" :stroke="btnStroke('undo')" stroke-width="0.8"/>
      <use href="#ico-undo" :x="TS_X+5" :y="DISPLAY_Y+4" width="10" height="10" color="#888" pointer-events="none"/>
    </g>

    <!-- Save — bottom-left of display, same column & width as touch_strip -->
    <g @mousedown="pressElem('save')" @mouseup="releaseElem('save')"
       @mouseenter="hoveredName='save'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="TS_X" :y="DISPLAY_Y+DISPLAY_H-LP_BTN_H" :width="RC_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('save')" :stroke="btnStroke('save')" stroke-width="0.8"/>
      <use href="#ico-save" :x="TS_X+5" :y="DISPLAY_Y+DISPLAY_H-LP_BTN_H+4" width="10" height="10" color="#888" pointer-events="none"/>
    </g>

    <!-- lock / stop_clip / mute / solo  — same row as media buttons -->
    <g v-for="(name,si) in LP_SMALL_MID_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="12+si*SMALL_PITCH" :y="LOWER_BTN_Y" :width="SMALL_BTN" :height="SMALL_BTN" rx="2"
            :fill="btnFill(name,'#1e1e22')" :stroke="btnStroke(name,'#3a3a3c')" stroke-width="0.7"/>
      <use :href="'#ico-'+name" :x="12+si*SMALL_PITCH+2" :y="LOWER_BTN_Y+2" width="10" height="10"
           color="#777" pointer-events="none"/>
    </g>

    <!-- Tempo knob -->
    <g :transform="`translate(${TEMPO_X},${TEMPO_Y})`"
       @mousedown="e=>startBigKnobDrag(swingVal,'tempo','tempo',e)"
       @mouseenter="hoveredName='tempo'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" :r="TEMPO_R" :fill="bindingForName('tempo')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.2"/>
      <circle cx="0" cy="0" :r="TEMPO_R-4" fill="none" stroke="#2e2e32" stroke-width="0.6"/>
      <line v-if="bigKnobDragKey==='tempo'" x1="0" y1="-4" x2="0" y2="-13"
            stroke="#18c8da" stroke-width="1.8" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.tempo})`"/>
    </g>

    <!-- Touch strip -->
    <rect ref="tsRectRef" :x="TS_X" :y="TS_Y" :width="TS_W" :height="TS_H" rx="4"
          fill="#0a0a0e" stroke="#222" stroke-width="0.8" style="cursor:ns-resize"
          @mousedown.prevent="tsStart"
          @mouseenter="hoveredName='touch_strip'" @mouseleave="hoveredName=null"/>
    <rect :x="TS_X+2" :y="TS_Y+2+(1-tsPos)*(TS_H-4)" :width="TS_W-4" :height="tsPos*(TS_H-4)" rx="2"
          fill="#18c8da" opacity="0.4" pointer-events="none"/>

    <!-- LP bottom buttons (tap_tempo, metronome, …) -->
    <g v-for="(name,bi) in LP_BTNS_BOT_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect x="12" :y="PADS_Y+68+bi*22" width="58" :height="LP_BTN_H" rx="2"
            :fill="btnFill(name)" :stroke="btnStroke(name)" stroke-width="0.8"/>
    </g>

    <!-- ═══ CENTER ═══ -->

    <!-- Subpage buttons (profile: subpage_1..8) -->
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
          font-family="monospace" font-size="7" fill="#18c8da" opacity="0.15" letter-spacing="3">PUSH 3</text>

    <!-- Media buttons (profile: media_1..8) -->
    <g v-for="i in 8" :key="'ldb-'+i"
       @mousedown="pressElem(`media_${i}`,127)" @mouseup="releaseElem(`media_${i}`)"
       @mouseenter="hoveredName=`media_${i}`" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="DISP_START_X+(i-1)*(DISP_BTN_W+4)" :y="LOWER_BTN_Y" :width="DISP_BTN_W" :height="DISP_BTN_H" rx="2"
            :fill="btnFill(`media_${i}`,'#1e1e22')" :stroke="btnStroke(`media_${i}`,'#353538')" stroke-width="0.8"/>
    </g>

    <!-- master_trk — right column, same row as media buttons -->
    <g @mousedown="pressElem('master_trk')" @mouseup="releaseElem('master_trk')"
       @mouseenter="hoveredName='master_trk'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RC_X" :y="LOWER_BTN_Y" :width="RC_W" :height="DISP_BTN_H" rx="2"
            :fill="btnFill('master_trk','#1e1e22')" :stroke="btnStroke('master_trk','#3a3a3c')" stroke-width="0.7"/>
    </g>

    <!-- Pad grid -->
    <g v-for="i in 64" :key="'pad-'+i"
       @mousedown="pressElem(padName(i-1))" @mouseup="releaseElem(padName(i-1))"
       @mouseenter="hoveredName=padName(i-1)" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="padX(i-1)" :y="padY(i-1)" :width="PAD_W" :height="PAD_H" rx="2"
            :fill="padFill(i-1)" :stroke="padColors[i-1]?'none':btnStroke(padName(i-1),'#2a2a2e')" stroke-width="0.8"/>
    </g>

    <!-- Scene launch — right column, aligned with pad rows -->
    <g v-for="i in 8" :key="'scene-'+i"
       @mousedown="pressElem(`scene_${i}`)" @mouseup="releaseElem(`scene_${i}`)"
       @mouseenter="hoveredName=`scene_${i}`" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RC_X" :y="PADS_Y+(i-1)*(SCENE_BTN_H+PAD_GAP)" :width="RC_W" :height="SCENE_BTN_H" rx="2"
            :fill="btnFill(`scene_${i}`,'#1e1e22')" :stroke="btnStroke(`scene_${i}`,'#2a2a2e')" stroke-width="0.8"/>
    </g>

    <!-- ═══ RIGHT PANEL ═══ -->

    <!-- device / mix / clip / session_view — centered above jog wheel -->
    <g v-for="(name,si) in RP_SMALL_TOP_NAMES" :key="name"
       @mousedown="pressElem(name)" @mouseup="releaseElem(name)"
       @mouseenter="hoveredName=name" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X-31+si*SMALL_PITCH" :y="DISP_BTN_Y" :width="SMALL_BTN" :height="SMALL_BTN" rx="2"
            :fill="btnFill(name,'#1e1e22')" :stroke="btnStroke(name,'#3a3a3c')" stroke-width="0.7"/>
      <use :href="'#ico-'+name" :x="JOG_X-31+si*SMALL_PITCH+2" :y="DISP_BTN_Y+2" width="10" height="10"
           color="#777" pointer-events="none"/>
    </g>

    <!-- Add — top of right column (mirrors undo) -->
    <g @mousedown="pressElem('add')" @mouseup="releaseElem('add')"
       @mouseenter="hoveredName='add'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RC_X" :y="DISPLAY_Y" :width="RC_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('add','#1e3a3e')" :stroke="btnStroke('add','#18c8da')" stroke-width="0.8"/>
      <text :x="RC_X+RC_W/2" :y="DISPLAY_Y+LP_BTN_H/2+3" text-anchor="middle"
            font-family="sans-serif" font-size="9" fill="#18c8da88">+</text>
    </g>

    <!-- Swap — bottom of right column (mirrors save) -->
    <g @mousedown="pressElem('swap')" @mouseup="releaseElem('swap')"
       @mouseenter="hoveredName='swap'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="RC_X" :y="DISPLAY_Y+DISPLAY_H-LP_BTN_H" :width="RC_W" :height="LP_BTN_H" rx="2"
            :fill="btnFill('swap')" :stroke="btnStroke('swap')" stroke-width="0.8"/>
      <use href="#ico-swap" :x="RC_X+5" :y="DISPLAY_Y+DISPLAY_H-LP_BTN_H+4" width="10" height="10" color="#888" pointer-events="none"/>
    </g>

    <!-- Jog wheel -->
    <g :transform="`translate(${JOG_X},${JOG_Y})`"
       @mousedown="e=>startBigKnobDrag(jogVal,'jog','jog',e)"
       @mouseenter="hoveredName='jog'" @mouseleave="hoveredName=null" style="cursor:ns-resize">
      <circle cx="0" cy="0" :r="JOG_R" :fill="bindingForName('jog')?'#1e2428':'#1e1e20'" stroke="#484848" stroke-width="1.5"/>
      <circle cx="0" cy="0" :r="JOG_R-6" fill="none" stroke="#2e2e32" stroke-width="0.8"/>
      <circle cx="0" cy="0" r="10" fill="#181818" stroke="#3a3a3c" stroke-width="0.8"/>
      <line v-if="bigKnobDragKey==='jog'" x1="0" y1="-8" x2="0" y2="-25"
            stroke="#18c8da" stroke-width="2" stroke-linecap="round"
            :transform="`rotate(${bigKnobAngles.jog})`"/>
    </g>

    <!-- D-pad (below jog) -->
    <g @mousedown="pressElem('dpad_up')" @mouseup="releaseElem('dpad_up')"
       @mouseenter="hoveredName='dpad_up'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X-9" :y="JOG_Y+JOG_R+12" width="18" height="15" rx="2"
            :fill="btnFill('dpad_up')" :stroke="btnStroke('dpad_up')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_left')" @mouseup="releaseElem('dpad_left')"
       @mouseenter="hoveredName='dpad_left'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X-29" :y="JOG_Y+JOG_R+29" width="18" height="15" rx="2"
            :fill="btnFill('dpad_left')" :stroke="btnStroke('dpad_left')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_center')" @mouseup="releaseElem('dpad_center')"
       @mouseenter="hoveredName='dpad_center'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X-9" :y="JOG_Y+JOG_R+29" width="18" height="15" rx="9"
            :fill="btnFill('dpad_center','#303034')" :stroke="btnStroke('dpad_center')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_right')" @mouseup="releaseElem('dpad_right')"
       @mouseenter="hoveredName='dpad_right'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X+11" :y="JOG_Y+JOG_R+29" width="18" height="15" rx="2"
            :fill="btnFill('dpad_right')" :stroke="btnStroke('dpad_right')" stroke-width="0.7"/>
    </g>
    <g @mousedown="pressElem('dpad_down')" @mouseup="releaseElem('dpad_down')"
       @mouseenter="hoveredName='dpad_down'" @mouseleave="hoveredName=null" style="cursor:pointer">
      <rect :x="JOG_X-9" :y="JOG_Y+JOG_R+46" width="18" height="15" rx="2"
            :fill="btnFill('dpad_down')" :stroke="btnStroke('dpad_down')" stroke-width="0.7"/>
    </g>

    <!-- Decorative -->
    <text x="436" y="16" text-anchor="middle" font-family="monospace" font-size="7" fill="#333" letter-spacing="2">ABLETON PUSH 3</text>
    <line x1="152" y1="52" x2="719" y2="52" stroke="#1e1e22" stroke-width="0.5"/>

    <!-- Map mode hint -->
    <text v-if="isMap" x="436" y="556" text-anchor="middle" font-family="monospace" font-size="7" fill="#18c8da44">MAP MODE — click any element to assign a role</text>

    <!-- Unplaced bindings -->
    <template v-if="unmatchedBindings.length">
      <text x="152" y="547" font-family="monospace" font-size="6" fill="#555">UNPLACED</text>
      <g v-for="(b,bi) in unmatchedBindings" :key="b.key">
        <rect :x="152+bi*88" y="550" width="84" height="14" rx="2"
              :fill="props.highlighted===b.key?'#243a24':'#1e1e22'"
              :stroke="props.highlighted===b.key?'#a0e060':'#444'"
              :stroke-width="props.highlighted===b.key?'2':'0.8'"/>
        <text :x="152+bi*88+42" y="559" text-anchor="middle" font-family="monospace" font-size="6.5"
              :fill="props.highlighted===b.key?'#a0e060':'#888'">{{ b.label }}</text>
      </g>
    </template>

    <!-- Highlight overlay -->
    <template v-if="highlightOverlay">
      <rect v-if="highlightOverlay.shape==='rect'"
            :x="highlightOverlay.x" :y="highlightOverlay.y" :width="highlightOverlay.w" :height="highlightOverlay.h" :rx="highlightOverlay.rx"
            fill="none" stroke="#a0e060" stroke-width="2.5" pointer-events="none"/>
      <circle v-else-if="highlightOverlay.shape==='circle'"
              :cx="highlightOverlay.cx" :cy="highlightOverlay.cy" :r="highlightOverlay.r"
              fill="none" stroke="#a0e060" stroke-width="2.5" pointer-events="none"/>
    </template>

    <!-- Hover tooltip -->
    <g v-if="hoveredName"
       :transform="`translate(${Math.min(tooltipPos.x+10,700)},${Math.min(tooltipPos.y+10,520)})`"
       pointer-events="none">
      <rect x="0" y="0" width="150" height="46" rx="3" fill="#0d0d10" stroke="#18c8da55" stroke-width="0.8" opacity="0.97"/>
      <text x="7" y="13" font-family="monospace" font-size="8" font-weight="bold" fill="#ddd">{{ hoveredName }}</text>
      <text x="7" y="26" font-family="monospace" font-size="6.5" fill="#18c8da">{{ tooltipMidi }}</text>
      <text x="7" y="38" font-family="monospace" font-size="6.5" :fill="tooltipRole==='—'?'#444':'#4caf50'">{{ tooltipRole }}</text>
    </g>
  </svg>
</template>

<style scoped>
.push3-svg { display:block; width:100%; max-width:880px; height:auto; }
</style>
