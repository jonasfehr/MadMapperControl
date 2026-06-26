<template>
  <div class="mapping-panel">

    <!-- ── Header ── -->
    <div class="mp-header">
      <!-- Controller selector -->
      <div class="profile-selector">
        <label class="profile-label">Controller</label>
        <div class="profile-btns">
          <button
            v-for="p in allProfiles" :key="p.name"
            class="profile-btn"
            :class="{ active: selectedProfileName === p.name, connected: p.active }"
            @click="selectProfile(p.name)"
          >
            <span v-if="p.active" class="conn-dot"></span>
            {{ p.name }}
          </button>
        </div>
      </div>

      <!-- Learn state banner -->
      <div class="learn-state" :class="learnPhase">
        <template v-if="learnPhase === 'idle'">
          <span class="learn-hint" v-if="surfaceMode === 'learn'">In Learn mode: click a surface element to name it, then press the matching control on the physical controller</span>
          <span class="learn-hint" v-else>Click <strong>Learn</strong> on a component row, then press a control on the physical controller</span>
        </template>
        <template v-else-if="learnPhase === 'waiting'">
          <span class="pulse-dot">●</span>
          <span>Learning <strong>{{ learningLabel }}</strong> — press the control on the <em>physical</em> controller</span>
          <button class="btn-xs" @click="cancelLearn">Cancel</button>
        </template>
        <template v-else-if="learnPhase === 'captured'">
          <span class="ok-dot">✓</span>
          <span>Captured: <strong>{{ capturedDesc }}</strong> — choose a role below then confirm</span>
          <button class="btn-xs" @click="cancelLearn">Cancel</button>
        </template>
      </div>

      <div class="mp-header-actions">
        <button class="btn-secondary" @click="reload" :disabled="loading">Reload</button>
        <button class="btn-primary" @click="saveAll" :disabled="saving">{{ saving ? 'Saving…' : 'Save All' }}</button>
      </div>
    </div>

    <div v-if="loading" class="mp-state">Loading…</div>
    <div v-else-if="error" class="mp-state error">{{ error }}</div>

    <div v-else class="mp-body">

      <!-- ══ LEFT: components ══ -->
      <section class="mp-col mp-col-left">
        <div class="col-title">Components</div>

        <div class="comp-list">
          <div
            v-for="comp in components" :key="comp.label"
            class="comp-row"
            :class="{
              learning:  learningLabel === comp.label,
              captured:  learnPhase === 'captured' && learningLabel === comp.label,
              'has-role': getRoleForLabel(comp.label),
              selected:  selectedCompLabel === comp.label
            }"
            @click="selectedCompLabel = selectedCompLabel === comp.label ? null : comp.label"
          >
            <span class="bound-dot" :class="{ bound: getRoleForLabel(comp.label) }"></span>
            <div class="comp-info">
              <template v-if="editingLabel === comp.label">
                <input class="label-input" :value="comp.label"
                  @click.stop
                  @blur="finishLabelEdit(comp, $event.target.value)"
                  @keydown.enter="finishLabelEdit(comp, $event.target.value)"
                  @keydown.escape="editingLabel = null" autofocus />
              </template>
              <template v-else>
                <span class="comp-label" @dblclick.stop="editingLabel = comp.label" title="Double-click to rename">{{ comp.label }}</span>
              </template>
              <span class="comp-midi">{{ midiDisplay(comp) }}</span>
            </div>
            <span class="comp-role-badge" v-if="getRoleForLabel(comp.label)">{{ shortRole(getRoleForLabel(comp.label)) }}</span>
            <button class="btn-xs danger" @click.stop="deleteComponent(comp.label)" title="Delete">✕</button>
          </div>
        </div>

        <!-- MIDI log -->
        <div class="midi-log">
          <div class="log-header">
            <span class="log-title">MIDI Monitor</span>
            <span v-if="midiLog.length" class="log-count">{{ midiLog.length }}</span>
            <button class="clear-btn" @click="midiLog = []">Clear</button>
          </div>
          <div class="log-entries">
            <div v-for="(e, i) in midiLog" :key="i" class="log-entry">
              <span class="log-time">{{ e.t }}</span>
              <span class="log-msg">{{ e.msg }}</span>
            </div>
            <div v-if="!midiLog.length" class="log-empty">— interact to see events —</div>
          </div>
        </div>
      </section>

      <!-- ══ CENTER: mock surface ══ -->
      <section class="mp-col mp-col-center">
        <div class="col-title">
          Surface
          <span class="page-badge" v-if="currentPageName">{{ currentPageName }}</span>
          <div class="mode-switcher">
            <button class="mode-btn" :class="{ active: surfaceMode === 'emulator' }" @click="surfaceMode = 'emulator'" title="Inject MIDI as if physical controller">Emulator</button>
            <button class="mode-btn" :class="{ active: surfaceMode === 'learn' }"   @click="surfaceMode = 'learn'"   title="Touch surface → learn MIDI + assign role">Learn</button>
            <button class="mode-btn" :class="{ active: surfaceMode === 'map' }"     @click="surfaceMode = 'map'"     title="Click element → reassign role without MIDI">Map</button>
          </div>
        </div>
        <div class="surface-wrap">
          <component
            :is="surfaceComponent"
            :channels="deviceChannels"
            :components="components"
            :bindings="surfaceBindings"
            :mode="surfaceMode"
            :highlighted="highlightedMidiKey"
            @midi-input="onMidiInput"
            @bank-change="onBankChange"
          />
        </div>
        <!-- bank nav -->
        <div v-if="totalBanks > 1" class="bank-nav">
          <button class="btn-xs" @click="bankOffset = Math.max(0, bankOffset - 1)" :disabled="bankOffset === 0">‹</button>
          <span class="bank-label">Bank {{ bankOffset + 1 }}/{{ totalBanks }}</span>
          <button class="btn-xs" @click="bankOffset = Math.min(totalBanks - 1, bankOffset + 1)" :disabled="bankOffset >= totalBanks - 1">›</button>
        </div>
      </section>

      <!-- ══ RIGHT: targets ══ -->
      <section class="mp-col mp-col-right">
        <div class="col-title">Mapping Targets</div>

        <!-- Nav actions -->
        <div class="target-group">
          <div class="group-label">Navigation</div>
          <div v-for="a in NAV_ACTIONS" :key="a.role" class="target-row">
            <span class="target-role">{{ a.role }}</span>
            <span class="target-arrow">→</span>
            <span class="target-desc">{{ a.desc }}</span>
            <span class="target-bound" :class="{ bound: getBound(a.role) }">{{ getBound(a.role) || '—' }}</span>
          </div>
        </div>

        <!-- Page shortcuts -->
        <div class="target-group">
          <div class="group-label">
            Page Shortcuts
            <button class="btn-xs" @click="addPageGoto">+ Add</button>
          </div>
          <div v-for="(entry, idx) in pageGotoList" :key="idx" class="target-row">
            <span class="target-role">nav.pageGoto.{{ idx }}</span>
            <span class="target-arrow">→</span>
            <select class="target-select" v-model="pageGotoList[idx]">
              <option value="">— select page —</option>
              <option v-for="pg in pages" :key="pg" :value="pg">{{ pg }}</option>
            </select>
            <span class="target-bound" :class="{ bound: getBound('nav.pageGoto.' + idx) }">{{ getBound('nav.pageGoto.' + idx) || '—' }}</span>
            <button class="btn-xs danger" @click="removePageGoto(idx)">✕</button>
          </div>
          <div v-if="!pageGotoList.length" class="empty-hint">No shortcuts yet.</div>
        </div>

        <!-- Fixed OSC -->
        <div v-for="(group, srvId) in fixedByServer" :key="srvId" class="target-group">
          <div class="group-label">
            Fixed OSC — {{ serverName(Number(srvId)) }}
            <button class="btn-xs" @click="addFixed(Number(srvId))">+ Add</button>
          </div>
          <div v-for="(fm, key) in group" :key="key" class="target-row fixed-row">
            <template v-if="editingFixedKey === key">
              <input class="label-input fixed-key-input" :value="key"
                @blur="finishFixedKeyEdit(key, $event.target.value)"
                @keydown.enter="finishFixedKeyEdit(key, $event.target.value)"
                @keydown.escape="editingFixedKey = null" autofocus />
            </template>
            <span v-else class="target-role" @dblclick="editingFixedKey = key" title="Double-click to rename">fixed.{{ key }}</span>
            <span class="target-arrow">→</span>
            <div class="fixed-editor">
              <input class="fixed-path" :value="fm.path" placeholder="/osc/path" @input="fm.path = $event.target.value" />
              <select class="fixed-mode" :value="fm.mode" @change="fm.mode = $event.target.value">
                <option value="absolute">absolute</option>
                <option value="delta">delta</option>
              </select>
            </div>
            <span class="target-bound" :class="{ bound: getBound('fixed.' + key) }">{{ getBound('fixed.' + key) || '—' }}</span>
            <button class="btn-xs danger" @click="removeFixed(key)">✕</button>
          </div>
        </div>
        <div class="target-group" v-if="config?.servers?.length > fixedServerCount">
          <button class="btn-xs" @click="addFixedToNewServer">+ Fixed mapping for other server</button>
        </div>

        <!-- Parameter bindings -->
        <div class="target-group" v-if="paramBindings.length">
          <div class="group-label">Parameter Slots</div>
          <div v-for="b in paramBindings" :key="b.role" class="target-row">
            <span class="target-role">{{ b.role }}</span>
            <span class="target-arrow">→</span>
            <span class="target-bound bound">{{ b.label }}</span>
          </div>
        </div>
      </section>
    </div>

    <!-- ══ Assign modal ══ -->
    <div v-if="assignModal" class="modal-backdrop" @click.self="closeAssign">
      <div class="modal">
        <div class="modal-title">
          Map "{{ assignModal.label }}"
          <span v-if="learnResult.type" class="type-badge" :class="learnResult.type">{{ learnResult.type?.toUpperCase() }}</span>
        </div>
        <div class="modal-body">
          <template v-if="learnResult.ready">
            <div class="picker-row">
              <label class="picker-label">Interface</label>
              <div class="picker-options">
                <button v-for="it in ifTypeOpts" :key="it.value" class="picker-btn" :class="{ active: assignIfType === it.value }" @click="assignIfType = it.value; assignCMT = suggestCMT(learnResult.type, it.value)">{{ it.icon }} {{ it.label }}</button>
              </div>
            </div>
            <div class="picker-row">
              <label class="picker-label">Message type</label>
              <div class="picker-options">
                <button v-for="ct in cmtOptions(learnResult.type)" :key="ct.value" class="picker-btn" :class="{ active: assignCMT === ct.value }" @click="assignCMT = ct.value">{{ ct.label }}</button>
              </div>
            </div>
            <div class="picker-divider" />
          </template>
          <div class="modal-hint">Assign a role:</div>

          <div class="role-group">
            <div class="role-group-title">Navigation</div>
            <div v-for="a in NAV_ACTIONS" :key="a.role" class="role-option" :class="{ selected: assignRole === a.role }" @click="assignRole = a.role">{{ a.role }}</div>
            <div v-for="(entry, idx) in pageGotoList" :key="'pg'+idx" class="role-option" :class="{ selected: assignRole === 'nav.pageGoto.' + idx }" @click="assignRole = 'nav.pageGoto.' + idx">nav.pageGoto.{{ idx }} → {{ entry || '(unset)' }}</div>
          </div>
          <div class="role-group">
            <div class="role-group-title">Fixed OSC</div>
            <div v-for="(fm, key) in fixedMappings" :key="key" class="role-option" :class="{ selected: assignRole === 'fixed.' + key }" @click="assignRole = 'fixed.' + key">fixed.{{ key }} <span class="role-path">{{ fm.path }}</span></div>
          </div>
          <div class="role-group">
            <div class="role-group-title">Parameter slot</div>
            <div class="param-sub-row">
              <button v-for="s in PARAM_SUBS" :key="s.value"
                class="picker-btn" :class="{ active: paramSub === s.value }"
                @click="paramSub = s.value; if (paramSlot) assignRole = 'param.' + paramSlot + '.' + s.value">
                {{ s.label }}
              </button>
            </div>
            <div class="param-grid">
              <div v-for="n in 16" :key="n" class="role-option param-opt"
                :class="{ selected: assignRole === 'param.' + n + '.' + paramSub }"
                @click="paramSlot = n; assignRole = 'param.' + n + '.' + paramSub">{{ n }}</div>
            </div>
          </div>
          <div class="role-group">
            <div class="role-group-title">Custom / clear</div>
            <input class="fixed-path" v-model="assignRole" placeholder="type a role…" />
            <div class="role-option" :class="{ selected: assignRole === '' }" @click="assignRole = ''">(clear binding)</div>
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn-secondary" @click="closeAssign">Cancel</button>
          <button class="btn-primary" @click="confirmAssign">Assign</button>
        </div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, reactive, computed, shallowRef, toRef, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { apiClient } from '../api.js'
import { useDeviceMappings } from '../composables/useDeviceMappings.js'
import Push3Surface      from './mock/Push3Surface.vue'
import Faderport16Surface from './mock/Faderport16Surface.vue'
import PlatformMSurface   from './mock/PlatformMSurface.vue'

const props = defineProps({
  config:        { type: Object, default: () => null },
  pages:         { type: Object, default: () => ({ pages: [], subpages: [] }) },
  allParameters: { type: Array,  default: () => [] },
})

// ── Surface devices ───────────────────────────────────────────────
const DEVICES = [
  { id: 'push3',     label: 'Ableton Push 3',  component: Push3Surface,      maxCh: 64 },
  { id: 'fp16',      label: 'Faderport 16',     component: Faderport16Surface, maxCh: 14 },
  { id: 'platformm', label: 'Platform M+',      component: PlatformMSurface,  maxCh: 8  },
]

// ── State ─────────────────────────────────────────────────────────
const loading   = ref(true)
const saving    = ref(false)
const error     = ref(null)

const allProfiles       = ref([])
const selectedProfileName = ref('')
const profileObj        = ref({})    // current profile being edited

const NAV_ACTIONS = [
  { role: 'nav.pageNext', desc: 'Next Page' },
  { role: 'nav.pagePrev', desc: 'Prev Page' },
  { role: 'nav.bankNext', desc: 'Next Bank' },
  { role: 'nav.bankPrev', desc: 'Prev Bank' },
  { role: 'nav.reload',   desc: 'Reload'    },
  { role: 'action.back',  desc: 'Go Back'   },
]
const PARAM_SUBS = [
  { value: 'val_ctrl',       label: 'Value' },
  { value: 'layerSubpage', label: 'Layer' },
  { value: 'mediaSubpage',   label: 'Media' },
  { value: 'groupSubpage',   label: 'Group' },
]

const pageGotoList  = reactive([])
const fixedMappings = reactive({})
const pages         = ref([])

const editingLabel    = ref(null)
const editingFixedKey = ref(null)

// ── MIDI learn state machine ──────────────────────────────────────
// phases: idle | waiting | captured
const learnPhase    = ref('idle')
const learningLabel = ref(null)
const learnResult   = ref({})
const learnMessages = ref([])
let   learnPollTimer    = null
let   captureWindowTimer = null   // extra window to collect CC after touch-note

const capturedDesc = computed(() => {
  const r = learnResult.value
  if (!r.type) return ''
  return r.type === 'cc'   ? `CC ${r.control} ch${r.channel}`
       : r.type === 'note' ? `Note ${r.pitch} ch${r.channel}`
       : `Pitch Bend ch${r.channel}`
})

// ── Mock surface ──────────────────────────────────────────────────
const bankOffset       = ref(0)
const surfaceMode      = ref('emulator')   // 'emulator' | 'learn' | 'map'
const selectedCompLabel = ref(null)

const highlightedMidiKey = computed(() => {
  if (!selectedCompLabel.value) return null
  const comp = components.value.find(c => c.label === selectedCompLabel.value)
  if (!comp) return null
  const mt = comp.type.startsWith('note') ? 'note'
           : comp.type === 'pitch_bend'   ? 'pitch_bend'
           : 'cc'
  return `${mt}:${comp.channel}:${comp.address}`
})

// Map profile name → device id for visual selection
function profileToDeviceId(name = '') {
  const n = name.toLowerCase()
  if (n.includes('push'))     return 'push3'
  if (n.includes('fader'))    return 'fp16'
  if (n.includes('platform')) return 'platformm'
  return 'push3'
}

const surfaceDeviceId = computed(() => profileToDeviceId(selectedProfileName.value))
const surfaceDevice   = computed(() => DEVICES.find(d => d.id === surfaceDeviceId.value) || DEVICES[0])
const surfaceComponent = computed(() => surfaceDevice.value.component)

// surfaceBindings: "midiType:channel:address" → { label, role }
// Exact type+address matching only — prevents showing on wrong physical button
const surfaceBindings = computed(() => {
  const map = {}
  for (const comp of components.value) {
    if (!comp.role) continue
    const mt = comp.type.startsWith('note') ? 'note'
             : comp.type === 'pitch_bend'   ? 'pitch_bend'
             : 'cc'
    map[`${mt}:${comp.channel}:${comp.address}`] = { label: comp.label, role: comp.role }
  }
  return map
})

const { resolvedChannels, currentPageName } = useDeviceMappings(
  toRef(props, 'pages'),
  toRef(props, 'config'),
  toRef(props, 'allParameters')
)

watch(currentPageName, () => { bankOffset.value = 0 })

const deviceChannels = computed(() => {
  const max   = surfaceDevice.value.maxCh
  const start = bankOffset.value * max
  return resolvedChannels.value.slice(start, start + max)
})
const totalBanks = computed(() => Math.max(1, Math.ceil(resolvedChannels.value.length / surfaceDevice.value.maxCh)))

// ── MIDI log ──────────────────────────────────────────────────────
const midiLog = ref([])
function logMidi(msg) {
  midiLog.value.unshift({ t: new Date().toISOString().slice(11, 23), msg })
  if (midiLog.value.length > 80) midiLog.value.length = 80
}

// ── Mock surface event handlers ───────────────────────────────────
const STATUS_NOTE_ON = 144
const STATUS_CC      = 176
const STATUS_PITCH   = 224

async function injectMidi({ type, channel, address, value }) {
  try {
    await apiClient.learnInject({
      channel,
      status:  type === 'note' ? STATUS_NOTE_ON : type === 'cc' ? STATUS_CC : STATUS_PITCH,
      control: type === 'cc'   ? address : 0,
      pitch:   type === 'note' ? address : 0,
      value
    })
  } catch (_) {}
}

function onMidiInput(ev) {
  const { type, channel, address, value, name: elemName } = ev

  // Surface element not yet in profile — create stub and start learn to capture its MIDI address
  if (type === '__learn_click__') {
    if (surfaceMode.value !== 'map' && surfaceMode.value !== 'learn') return
    const lbl = elemName
    if (!lbl) return
    let comp = components.value.find(c => c.label === lbl)
    if (!comp) {
      comp = { label: lbl, channel: 1, type: 'note_toggle', address: 0, interfaceType: 'button' }
      if (!profileObj.value.components) profileObj.value.components = []
      profileObj.value.components.push(comp)
    }
    startLearnFor(lbl)
    return
  }

  const logLabel = type === 'note' ? `Note ${value > 0 ? 'On' : 'Off'} ch:${channel} note:${address} vel:${value}`
                 : type === 'cc'   ? `CC ch:${channel} cc:${address} val:${value}`
                 :                   `Pitch ch:${channel} val:${value}`
  logMidi(logLabel)

  if (surfaceMode.value === 'map') {
    // map mode: open assign modal on press; no MIDI inject
    if (value === 0) return
    const mt = type === 'note' ? 'note' : type === 'cc' ? 'cc' : 'pitch_bend'
    const comp = components.value.find(c => {
      const ct = c.type.startsWith('note') ? 'note' : c.type === 'pitch_bend' ? 'pitch_bend' : 'cc'
      return ct === mt && c.channel === channel && c.address === address
    })
    if (comp) {
      openAssign(comp)
    } else {
      // use the layout element name emitted by the surface — no prompt needed
      const lbl = elemName || prompt('Label for new component:')?.trim()
      if (!lbl) return
      const newComp = {
        label: lbl,
        channel,
        type: type === 'note' ? 'note_toggle' : type === 'cc' ? 'cc_toggle' : 'pitch_bend',
        address,
        interfaceType: type === 'note' ? 'button' : type === 'cc' ? 'knob' : 'fader'
      }
      if (!profileObj.value.components) profileObj.value.components = []
      profileObj.value.components.push(newComp)
      openAssign(newComp)
    }
    return
  }

  if (surfaceMode.value === 'learn') {
    if (value === 0) return  // ignore release events

    // Find existing component already bound to this surface element
    const mt = type === 'note' ? 'note' : type === 'cc' ? 'cc' : 'pitch_bend'
    const existing = surfaceBindings.value[`${mt}:${channel}:${address}`]

    if (existing) {
      // Re-learn an existing component: physical hardware will determine new type+address
      startLearnFor(existing.label)
    } else {
      // use the layout element name emitted by the surface — no prompt needed
      const lbl = elemName || prompt('Label for new component:')?.trim()
      if (!lbl) return
      const newComp = {
        label: lbl,
        channel,
        type: type === 'note' ? 'note_toggle' : type === 'cc' ? 'cc_toggle' : 'pitch_bend',
        address,
        interfaceType: type === 'note' ? 'button' : type === 'cc' ? 'knob' : 'fader'
      }
      if (!profileObj.value.components) profileObj.value.components = []
      profileObj.value.components.push(newComp)
      startLearnFor(lbl)
    }
    return
  }

  // emulator mode: inject MIDI as if this were the physical controller
  injectMidi(ev)
}

function onBankChange(delta) {
  bankOffset.value = Math.max(0, Math.min(totalBanks.value - 1, bankOffset.value + delta))
}

// ── Learn helpers ──────────────────────────────────────────────────
function startLearnFor(label) {
  learningLabel.value = label
  learnResult.value   = {}
  learnMessages.value = []
  learnPhase.value    = 'waiting'
  apiClient.learnStart().catch(() => {})
  startLearnPoll()
}

function cancelLearn() {
  learnPhase.value    = 'idle'
  learningLabel.value = null
  learnResult.value   = {}
  stopLearnPoll()
  apiClient.learnStop().catch(() => {})
}

// Pick the most informative message from the accumulated list.
// Knobs send a note (touch detect) then CC (actual movement) — prefer CC.
function pickBestMessage(msgs) {
  if (!Array.isArray(msgs) || !msgs.length) return null
  return msgs.find(m => m.type === 'cc') || msgs[msgs.length - 1]
}

function openAssignAfterCapture() {
  if (learningLabel.value === '__new__') {
    const label = prompt('Label for new component:')?.trim()
    if (!label) { cancelLearn(); return }
    const r = learnResult.value
    const newComp = {
      label,
      channel: r.channel,
      type: r.type === 'note' ? 'note_toggle' : r.type === 'cc' ? 'cc_toggle' : 'pitch_bend',
      address: r.type === 'note' ? r.pitch : r.control,
      interfaceType: r.type === 'cc' ? 'knob' : 'button'
    }
    if (!profileObj.value.components) profileObj.value.components = []
    profileObj.value.components.push(newComp)
    openAssignWithLearn(newComp)
  } else {
    const comp = components.value.find(c => c.label === learningLabel.value)
    if (comp) openAssignWithLearn(comp)
  }
}

function startLearnPoll() {
  stopLearnPoll()
  learnPollTimer = setInterval(async () => {
    try {
      const s = await apiClient.learnStatus()
      if (Array.isArray(s.messages)) learnMessages.value = s.messages

      if (s.ready && learnPhase.value === 'waiting' && !captureWindowTimer) {
        // Open a 500ms window so CC can arrive after a touch-note
        captureWindowTimer = setTimeout(() => {
          captureWindowTimer = null
          stopLearnPoll()
          const best = pickBestMessage(learnMessages.value)
          if (!best) { cancelLearn(); return }
          learnResult.value = {
            ready:   true,
            type:    best.type,
            channel: best.channel,
            control: best.control,
            pitch:   best.pitch,
          }
          learnPhase.value = 'captured'
          openAssignAfterCapture()
        }, 500)
      }
    } catch (_) {}
  }, 200)
}
function stopLearnPoll() {
  clearInterval(learnPollTimer); learnPollTimer = null
  clearTimeout(captureWindowTimer); captureWindowTimer = null
}

// ── Profile / component helpers ───────────────────────────────────
const components = computed(() => {
  const arr = profileObj.value?.components
  return Array.isArray(arr) ? arr : []
})
function getRoleForLabel(label) {
  return components.value.find(c => c.label === label)?.role || null
}
function getBound(role) {
  return components.value.find(c => c.role === role)?.label || null
}

const paramBindings = computed(() => {
  return components.value
    .filter(c => c.role?.startsWith('param.'))
    .map(c => ({ role: c.role, label: c.label }))
    .sort((a, b) => {
      const am = a.role.match(/^param\.(\d+)\.(.+)$/)
      const bm = b.role.match(/^param\.(\d+)\.(.+)$/)
      if (am && bm) { const nd = parseInt(am[1]) - parseInt(bm[1]); if (nd !== 0) return nd; return am[2].localeCompare(bm[2]) }
      return a.role.localeCompare(b.role)
    })
})

function syncParamPicker(role) {
  const m = role?.match(/^param\.(\d+)\.(.+)$/)
  if (m) { paramSlot.value = parseInt(m[1]); paramSub.value = m[2] }
  else   { paramSlot.value = null }
}

async function selectProfile(name) {
  selectedProfileName.value = name
  // Load this profile from allProfiles (no extra fetch needed)
  const found = allProfiles.value.find(p => p.name === name)
  if (found) profileObj.value = JSON.parse(JSON.stringify(found))
}

// ── Fixed mappings ────────────────────────────────────────────────
const fixedByServer = computed(() => {
  const groups = {}
  for (const [key, fm] of Object.entries(fixedMappings)) {
    const sid = String(fm.serverId ?? 0)
    if (!groups[sid]) groups[sid] = {}
    groups[sid][key] = fm
  }
  return groups
})
const fixedServerCount = computed(() => Object.keys(fixedByServer.value).length)
const servers = computed(() => props.config?.servers || [])
function serverName(idx) { return servers.value[idx]?.id || `Server ${idx}` }

// ── Display helpers ───────────────────────────────────────────────
function midiDisplay(comp) {
  if (!comp) return ''
  const ch   = comp.channel !== 1 ? ` ch${comp.channel}` : ''
  const t    = comp.type
  if (t === 'note' || t === 'note_toggle') return `Note ${comp.address}${ch}`
  if (t === 'pitch_bend')                  return `Pitch${ch}`
  if (t === 'encoder_relative')            return `CC ${comp.address} rel${ch}`
  return `CC ${comp.address}${ch}`
}
function shortRole(role) {
  if (!role) return ''
  if (role.startsWith('nav.'))    return role.slice(4)
  if (role.startsWith('action.')) return role.slice(7)
  if (role.startsWith('fixed.'))  return role.slice(6)
  const pm = role.match(/^param\.(\d+)\.(.+)$/)
  if (pm) {
    const abbr = { val_ctrl:'val', fader:'val', knob:'val', layerSubpage:'lyr', mediaSubpage:'mda', groupSubpage:'grp' }
    return `p${pm[1]}.${abbr[pm[2]] || pm[2]}`
  }
  return role
}

// ── Interface/CMT helpers ─────────────────────────────────────────
const ifTypeOpts = [
  { value: 'button',    label: 'Button',    icon: '□' },
  { value: 'button_lp', label: 'Button LP', icon: '■' },
  { value: 'knob',      label: 'Knob',      icon: '◎' },
  { value: 'fader',     label: 'Fader',     icon: '▮' },
]
function ifIcon(t) { return { knob: '◎', fader: '▮', button: '□', button_lp: '■' }[t] || '·' }
const CMT_OPTIONS = {
  cc:         [{ value:'cc', label:'CC (absolute)' }, { value:'cc_toggle', label:'CC Toggle' }, { value:'encoder', label:'Encoder (abs)' }, { value:'encoder_relative', label:'Encoder (rel)' }],
  note:       [{ value:'note', label:'Note (momentary)' }, { value:'note_toggle', label:'Note Toggle' }],
  pitch_bend: [{ value:'pitch_bend', label:'Pitch Bend' }],
}
function cmtOptions(t) { return CMT_OPTIONS[t] || CMT_OPTIONS.cc }
function suggestCMT(t, ifType) {
  if (t === 'note')       return 'note_toggle'
  if (t === 'pitch_bend') return 'pitch_bend'
  if (ifType === 'knob')  return 'encoder_relative'
  if (ifType === 'fader') return 'cc'
  return 'cc_toggle'
}

// ── Assign modal ──────────────────────────────────────────────────
const assignModal  = ref(null)
const assignRole   = ref('')
const assignIfType = ref('button')
const assignCMT    = ref('note_toggle')
const paramSlot    = ref(null)
const paramSub     = ref('fader')

function openAssign(comp) {
  assignModal.value  = comp
  assignRole.value   = getRoleForLabel(comp.label) || ''
  assignIfType.value = comp.interfaceType || 'button'
  assignCMT.value    = comp.type || 'note_toggle'
  syncParamPicker(assignRole.value)
}

function openAssignWithLearn(comp) {
  assignModal.value  = comp
  assignRole.value   = getRoleForLabel(comp.label) || ''
  assignIfType.value = comp.interfaceType || 'button'
  assignCMT.value    = suggestCMT(learnResult.value.type, assignIfType.value)
  syncParamPicker(assignRole.value)
}

function closeAssign() { assignModal.value = null }

async function confirmAssign() {
  if (!assignModal.value) return
  const comp = assignModal.value
  const role = assignRole.value

  if (learnResult.value.ready) {
    const lr = learnResult.value
    comp.interfaceType = assignIfType.value
    comp.type          = assignCMT.value
    comp.channel       = lr.channel || comp.channel
    comp.address       = lr.type === 'note' ? lr.pitch : lr.control
  }

  // Clear this role from any other component that currently holds it
  for (const c of profileObj.value.components || [])
    if (c.role === role && c.label !== comp.label) delete c.role
  // Set role on this component (overwrite previous role)
  if (role) comp.role = role
  else delete comp.role

  if (learnResult.value.ready) {
    const lr = learnResult.value
    await apiClient.learnAssign({
      label: comp.label, channel: lr.channel, role,
      address: lr.type === 'note' ? lr.pitch : lr.control,
      type: assignCMT.value, interfaceType: assignIfType.value
    })
  }

  closeAssign()
  learnPhase.value    = 'idle'
  learningLabel.value = null
  learnResult.value   = {}
  apiClient.learnStop().catch(() => {})
}

// ── Component editing ─────────────────────────────────────────────
function finishLabelEdit(comp, newLabel) {
  const trimmed = newLabel.trim()
  if (trimmed && trimmed !== comp.label) comp.label = trimmed
  editingLabel.value = null
}
function deleteComponent(label) {
  if (!profileObj.value.components) return
  const idx = profileObj.value.components.findIndex(c => c.label === label)
  if (idx >= 0) profileObj.value.components.splice(idx, 1)
}

// ── Page goto CRUD ────────────────────────────────────────────────
function addPageGoto()       { pageGotoList.push('') }
function removePageGoto(idx) { pageGotoList.splice(idx, 1) }

// ── Fixed mapping CRUD ────────────────────────────────────────────
function addFixed(serverId = 0) {
  const key = prompt('Role name (without "fixed." prefix):')
  if (!key) return
  fixedMappings[key] = reactive({ path: '', serverId, mode: 'absolute' })
}
function addFixedToNewServer() {
  const idx = parseInt(prompt(`Server index:\n${servers.value.map((s,i) => i+': '+s.id).join('\n')}`), 10)
  if (!isNaN(idx)) addFixed(idx)
}
function removeFixed(key) { delete fixedMappings[key] }
function finishFixedKeyEdit(oldKey, newKey) {
  const trimmed = newKey.trim()
  if (trimmed && trimmed !== oldKey) {
    const val = fixedMappings[oldKey]; delete fixedMappings[oldKey]; fixedMappings[trimmed] = val
    const comp = profileObj.value.components?.find(c => c.role === 'fixed.' + oldKey)
    if (comp) comp.role = 'fixed.' + trimmed
  }
  editingFixedKey.value = null
}

// ── Load / Save ───────────────────────────────────────────────────
async function reload() {
  loading.value = true; error.value = null
  try {
    const [profs, maps] = await Promise.all([apiClient.fetchProfiles(), apiClient.fetchMappings()])
    allProfiles.value = Array.isArray(profs) ? profs : []

    // Select currently active profile by default, or keep current selection
    const active = allProfiles.value.find(p => p.active)
    if (!selectedProfileName.value && active) selectedProfileName.value = active.name
    const current = allProfiles.value.find(p => p.name === selectedProfileName.value) || active
    if (current) profileObj.value = JSON.parse(JSON.stringify(current))

    // Mappings
    pageGotoList.length = 0
    if (Array.isArray(maps.nav?.pageGoto)) maps.nav.pageGoto.forEach(e => pageGotoList.push(e))

    Object.keys(fixedMappings).forEach(k => delete fixedMappings[k])
    if (maps.fixed && typeof maps.fixed === 'object')
      for (const [k, v] of Object.entries(maps.fixed))
        fixedMappings[k] = reactive({ path: v.path || '', serverId: v.serverId ?? 0, mode: v.mode || 'absolute' })

    pages.value = Array.isArray(maps.pages) ? maps.pages : []
  } catch (e) {
    error.value = e.message || 'Failed to load'
  } finally {
    loading.value = false
  }
}

async function saveAll() {
  saving.value = true
  try {
    const payload = {
      nav: { pageGoto: [...pageGotoList] },
      fixed: Object.fromEntries(Object.entries(fixedMappings).map(([k,v]) => [k, { path: v.path, serverId: v.serverId, mode: v.mode }]))
    }
    await Promise.all([
      apiClient.saveMappings(payload),
      apiClient.saveProfile({ ...profileObj.value })
    ])
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

onMounted(() => { reload() })
onUnmounted(() => { stopLearnPoll(); apiClient.learnStop().catch(() => {}) })
</script>

<style scoped>
.mapping-panel {
  display: flex; flex-direction: column; height: 100%; overflow: hidden;
  background: var(--bg-panel); position: relative;
}

/* ── Header ── */
.mp-header {
  display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  padding: 8px 12px; border-bottom: 1px solid var(--border-strong);
  background: var(--bg-shell); flex-shrink: 0;
}
.profile-selector { display: flex; align-items: center; gap: 8px; flex-shrink: 0; }
.profile-label { font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; color: var(--text-dim); }
.profile-btns  { display: flex; gap: 4px; }
.profile-btn {
  height: 24px; padding: 0 9px; border-radius: 3px; font-size: 11px; font-weight: 600;
  border: 1px solid var(--border-strong); background: var(--bg-panel-soft); color: var(--text-dim); cursor: pointer;
  display: flex; align-items: center; gap: 5px;
}
.profile-btn:hover { color: var(--text-main); }
.profile-btn.active { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.conn-dot { width: 5px; height: 5px; border-radius: 50%; background: #4caf50; flex-shrink: 0; }

/* Learn state */
.learn-state {
  flex: 1; display: flex; align-items: center; gap: 8px;
  padding: 4px 10px; border-radius: 4px; font-size: 11px; color: var(--text-dim);
  border: 1px solid transparent; min-width: 0;
}
.learn-state.waiting  { background: #1a2a1a; border-color: #3a6b3a; color: #80c880; }
.learn-state.captured { background: #1a2030; border-color: #2d5a7a; color: #80d8ff; }
.learn-hint { font-style: italic; color: var(--text-dim); }
.pulse-dot  { color: #4caf50; animation: pulse 1s infinite; }
.ok-dot     { color: #80d8ff; }
@keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }

.mp-header-actions { display: flex; gap: 6px; flex-shrink: 0; }
.mp-state { padding: 24px; color: var(--text-muted); font-size: 12px; }
.mp-state.error { color: #f44; }

/* ── Body ── */
.mp-body { display: flex; flex: 1; overflow: hidden; }

/* ── Columns ── */
.mp-col {
  display: flex; flex-direction: column; overflow: hidden;
  border-right: 1px solid var(--border-strong);
}
.mp-col:last-child { border-right: none; }
.mp-col-left   { width: 240px; flex-shrink: 0; }
.mp-col-center { flex: 1; min-width: 0; overflow: auto; }
.mp-col-right  { width: 300px; flex-shrink: 0; overflow-y: auto; }

.col-title {
  display: flex; align-items: center; gap: 8px;
  padding: 6px 10px; font-size: 10px; font-weight: 700; text-transform: uppercase;
  letter-spacing: 0.06em; color: var(--text-dim);
  background: var(--bg-panel-soft); border-bottom: 1px solid var(--border-soft); flex-shrink: 0;
}
.page-badge {
  padding: 1px 6px; border-radius: 3px; font-size: 9px; font-weight: 600;
  background: var(--bg-active); border: 1px solid var(--accent-dim); color: var(--accent);
}

/* ── Components list ── */
.comp-list { flex: 1; overflow-y: auto; }
.comp-row {
  display: flex; align-items: center; gap: 7px;
  padding: 5px 8px; border-bottom: 1px solid var(--border-soft);
  cursor: pointer; transition: background 0.08s;
}
.comp-row:hover    { background: var(--bg-hover); }
.comp-row.selected { background: var(--bg-active); }
.comp-row.learning { background: #131f13; }
.comp-row.captured { background: #131a1f; }

.bound-dot {
  width: 6px; height: 6px; border-radius: 50%; flex-shrink: 0;
  background: var(--border-strong); border: 1px solid var(--border-strong);
}
.bound-dot.bound { background: var(--accent); border-color: var(--accent); }

.comp-info  { flex: 1; min-width: 0; }
.comp-label {
  display: block; font-size: 11px; font-weight: 600;
  color: var(--text-muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.comp-row.has-role .comp-label { color: var(--text-main); }
.comp-row.selected .comp-label { color: var(--accent); }
.comp-midi  { display: block; font-size: 9px; color: var(--text-dim); font-family: monospace; letter-spacing: 0.02em; }
.comp-role-badge {
  font-size: 8px; font-weight: 700; font-family: monospace;
  color: var(--accent); background: var(--bg-active);
  border: 1px solid var(--accent-dim); border-radius: 3px;
  padding: 1px 4px; white-space: nowrap; flex-shrink: 0; max-width: 80px;
  overflow: hidden; text-overflow: ellipsis;
}

/* ── Surface ── */
.mode-switcher { display: flex; gap: 2px; margin-left: auto; }
.mode-btn {
  height: 18px; padding: 0 7px; border-radius: 3px; font-size: 9px; font-weight: 700;
  letter-spacing: 0.04em; text-transform: uppercase; cursor: pointer;
  border: 1px solid var(--border-strong); background: var(--bg-panel-soft); color: var(--text-dim);
  transition: color 0.1s, background 0.1s, border-color 0.1s;
}
.mode-btn:hover { background: var(--bg-hover); color: var(--text-muted); }
.mode-btn.active { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.surface-wrap { padding: 12px; overflow-x: auto; }
.bank-nav { display: flex; align-items: center; gap: 8px; padding: 6px 12px; border-top: 1px solid var(--border-soft); }
.bank-label { font-size: 11px; color: var(--text-muted); flex: 1; text-align: center; }

/* ── MIDI log ── */
.midi-log { border-top: 1px solid var(--border-strong); flex-shrink: 0; }
.log-header { display: flex; align-items: center; gap: 6px; padding: 3px 8px; background: var(--bg-shell); border-bottom: 1px solid var(--border-soft); }
.log-title  { font-size: 9px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; color: var(--text-dim); }
.log-count  { font-size: 9px; color: var(--text-dim); background: var(--bg-panel-soft); border-radius: 8px; padding: 0 5px; }
.clear-btn  { margin-left: auto; height: 16px; padding: 0 6px; font-size: 9px; border-radius: 2px; border: 1px solid var(--border-strong); background: transparent; color: var(--text-dim); cursor: pointer; }
.clear-btn:hover { color: var(--text-base); }
.log-entries { max-height: 100px; overflow-y: auto; font-family: monospace; font-size: 10px; }
.log-entry   { display: flex; gap: 8px; padding: 1px 8px; }
.log-entry:hover { background: var(--bg-panel-soft); }
.log-time    { color: var(--text-dim); flex-shrink: 0; }
.log-msg     { color: var(--accent); }
.log-empty   { padding: 6px 8px; color: var(--text-dim); font-size: 10px; }

/* ── Targets ── */
.target-group { padding: 0 0 4px; border-bottom: 1px solid var(--border-soft); }
.group-label {
  display: flex; align-items: center; justify-content: space-between;
  padding: 5px 10px; font-size: 9px; font-weight: 700; text-transform: uppercase;
  letter-spacing: 0.06em; color: var(--text-dim); background: var(--bg-panel-soft);
}
.target-row { display: flex; align-items: center; gap: 5px; padding: 4px 10px; border-bottom: 1px solid var(--border-soft); font-size: 10px; }
.target-row:last-child { border-bottom: none; }
.target-role  { width: 130px; flex-shrink: 0; font-family: monospace; font-size: 9px; color: var(--text-muted); }
.target-arrow { color: var(--text-dim); flex-shrink: 0; }
.target-desc  { flex: 1; color: var(--text-dim); font-size: 10px; }
.target-bound { width: 80px; flex-shrink: 0; font-size: 9px; color: var(--text-dim); text-align: right; font-family: monospace; }
.target-bound.bound { color: var(--accent); }
.target-select { flex: 1; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 10px; padding: 2px 3px; }
.empty-hint { padding: 6px 10px; color: var(--text-dim); font-size: 10px; font-style: italic; }
.fixed-row { flex-wrap: nowrap; }
.fixed-editor { flex: 1; display: flex; gap: 4px; min-width: 0; }
.fixed-path { flex: 1; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 10px; padding: 2px 4px; font-family: monospace; min-width: 0; }
.fixed-mode { width: 70px; flex-shrink: 0; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 10px; padding: 2px 3px; }
.fixed-key-input { width: 110px; background: var(--bg-panel-soft); border: 1px solid var(--accent-dim); border-radius: 3px; color: var(--text-main); font-size: 9px; font-family: monospace; padding: 1px 4px; }

/* ── Buttons ── */
.btn-primary, .btn-secondary, .btn-xs {
  border-radius: 3px; font-size: 11px; font-weight: 600; cursor: pointer;
  padding: 4px 10px; border: 1px solid transparent;
}
.btn-primary   { background: var(--accent); color: #000; border-color: var(--accent); }
.btn-primary:hover   { opacity: 0.85; }
.btn-primary:disabled { opacity: 0.4; cursor: default; }
.btn-secondary { background: var(--bg-panel-soft); color: var(--text-main); border-color: var(--border-strong); }
.btn-secondary:hover { background: var(--bg-hover); }
.btn-secondary:disabled { opacity: 0.4; cursor: default; }
.btn-xs { padding: 2px 6px; font-size: 10px; background: var(--bg-panel-soft); color: var(--text-muted); border-color: var(--border-soft); }
.btn-xs:hover { background: var(--bg-hover); color: var(--text-main); }
.btn-xs.learn-active { background: #1f3a1f; color: #4caf50; border-color: #3a6b3a; }
.btn-xs.danger:hover { background: #4d1a1a; color: #f44; border-color: #6b2d2d; }

/* Inline edit */
.label-input { width: 100%; background: var(--bg-panel-soft); border: 1px solid var(--accent-dim); border-radius: 3px; color: var(--text-main); font-size: 11px; font-weight: 600; padding: 1px 4px; outline: none; }

/* ── Modal ── */
.modal-backdrop { position: absolute; inset: 0; background: rgba(0,0,0,0.65); display: flex; align-items: center; justify-content: center; z-index: 100; }
.modal { background: var(--bg-shell); border: 1px solid var(--border-strong); border-radius: 6px; width: 460px; max-height: 82vh; display: flex; flex-direction: column; overflow: hidden; }
.modal-title { padding: 12px 16px; font-size: 13px; font-weight: 700; color: var(--text-main); border-bottom: 1px solid var(--border-strong); flex-shrink: 0; display: flex; align-items: center; gap: 8px; }
.modal-body  { flex: 1; overflow-y: auto; padding: 12px 16px; display: flex; flex-direction: column; gap: 10px; }
.modal-hint  { font-size: 11px; color: var(--text-muted); }
.modal-footer { display: flex; justify-content: flex-end; gap: 8px; padding: 10px 16px; border-top: 1px solid var(--border-strong); flex-shrink: 0; }

.type-badge { padding: 1px 6px; border-radius: 3px; font-size: 9px; font-weight: 800; letter-spacing: 0.08em; flex-shrink: 0; }
.type-badge.cc         { background: #1a3a5a; color: #80d8ff; border: 1px solid #2d6a9a; }
.type-badge.note       { background: #3a1a5a; color: #d0a0ff; border: 1px solid #6a2d9a; }
.type-badge.pitch_bend { background: #3a2a1a; color: #ffcc80; border: 1px solid #9a6a2d; }

.picker-row     { display: flex; align-items: center; gap: 10px; }
.picker-label   { width: 100px; flex-shrink: 0; font-size: 10px; font-weight: 700; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.06em; }
.picker-options { display: flex; gap: 4px; flex-wrap: wrap; }
.picker-btn { padding: 3px 10px; font-size: 11px; border-radius: 4px; cursor: pointer; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); color: var(--text-muted); font-weight: 500; }
.picker-btn:hover  { background: var(--bg-hover); color: var(--text-main); }
.picker-btn.active { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.picker-divider { border-top: 1px solid var(--border-soft); margin: 2px 0; }

.role-group { display: flex; flex-direction: column; gap: 2px; }
.role-group-title { font-size: 10px; font-weight: 700; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.06em; margin-bottom: 2px; }
.role-option { padding: 4px 8px; border-radius: 3px; cursor: pointer; font-size: 11px; color: var(--text-muted); font-family: monospace; border: 1px solid transparent; }
.role-option:hover    { background: var(--bg-hover); color: var(--text-main); }
.role-option.selected { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.role-path { color: var(--text-dim); font-size: 10px; margin-left: 6px; }
.param-sub-row { display: flex; gap: 3px; flex-wrap: wrap; margin-bottom: 4px; }
.param-grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 3px; }
.param-opt  { text-align: center; padding: 3px 4px; }
</style>
