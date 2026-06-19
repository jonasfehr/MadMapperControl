<template>
  <div class="mapping-panel">

    <!-- ── Header ── -->
    <div class="mp-header">
      <span class="mp-title">Controller Mapping</span>
      <div class="mp-header-actions">
        <button class="btn-secondary" @click="reload" :disabled="loading">Reload</button>
        <button class="btn-primary"   @click="saveAll" :disabled="saving">{{ saving ? 'Saving…' : 'Save All' }}</button>
      </div>
    </div>

    <div v-if="loading" class="mp-state">Loading…</div>
    <div v-else-if="error" class="mp-state error">{{ error }}</div>

    <div v-else class="mp-body">

      <!-- ══════════════════════════════════════════════
           LEFT: controller components
           ══════════════════════════════════════════════ -->
      <section class="mp-section">
        <div class="section-header">
          <span class="section-title">Controller Components</span>
          <span class="section-sub">{{ profile.name || '—' }}</span>
        </div>

        <!-- MIDI Learn status bar -->
        <div class="learn-bar" :class="{ active: learnActive || learnMessages.length > 0 }">
          <template v-if="learnActive || learnMessages.length > 0">
            <div class="learn-bar-top">
              <span class="learn-pulse" v-if="learnActive">●</span>
              <span v-if="learnActive">Touch hardware or use Emulator tab…</span>
              <span v-else class="learn-result">Capture complete — pick a message below</span>
              <button class="btn-xs" @click="stopLearn" v-if="learnActive">Stop</button>
              <button class="btn-xs" @click="cancelLearn" v-else>Dismiss</button>
            </div>
            <!-- Live message log -->
            <div v-if="learnMessages.length" class="learn-msg-log">
              <div
                v-for="(m, i) in [...learnMessages].reverse()"
                :key="i"
                class="learn-msg-row"
                :class="{ selected: selectedLearnMsg === learnMessages.length - 1 - i }"
                @click="selectLearnMsg(learnMessages.length - 1 - i)"
              >
                <span class="type-badge" :class="m.type">{{ m.type?.toUpperCase() }}</span>
                <span class="msg-detail">
                  {{ m.type === 'cc' ? 'CC ' + m.control : m.type === 'note' ? 'Note ' + m.pitch : 'Pitch' }}
                  &nbsp;ch {{ m.channel }}
                  &nbsp;· {{ m.value ?? '—' }}
                </span>
              </div>
            </div>
          </template>
          <template v-else>
            <span class="learn-hint">Click "Learn" next to a component to capture a MIDI control</span>
          </template>
        </div>

        <!-- Component table -->
        <div class="comp-list">
          <div
            v-for="comp in components"
            :key="comp.label"
            class="comp-row"
            :class="{ 'is-learning': learningLabel === comp.label, 'has-role': getRoleForLabel(comp.label) }"
          >
            <!-- Interface icon -->
            <span class="comp-icon" :title="comp.interfaceType">{{ interfaceIcon(comp.interfaceType) }}</span>

            <!-- Label (double-click to rename) -->
            <div class="comp-info">
              <template v-if="editingLabel === comp.label">
                <input
                  class="label-input"
                  :value="comp.label"
                  @blur="finishLabelEdit(comp, $event.target.value)"
                  @keydown.enter="finishLabelEdit(comp, $event.target.value)"
                  @keydown.escape="editingLabel = null"
                  ref="labelInputRef"
                  autofocus
                />
              </template>
              <template v-else>
                <span
                  class="comp-label"
                  @dblclick="startLabelEdit(comp.label)"
                  title="Double-click to rename"
                >{{ comp.label }}</span>
              </template>
              <span class="comp-detail">
                ch {{ comp.channel }} ·
                {{ comp.type === 'note' || comp.type === 'note_toggle' ? 'note ' + comp.address : comp.type === 'pitch_bend' ? 'pitch' : 'CC ' + comp.address }}
                · {{ comp.type }}
              </span>
            </div>

            <!-- Role badge -->
            <div class="comp-role">
              <span v-if="getRoleForLabel(comp.label)" class="role-badge">{{ getRoleForLabel(comp.label) }}</span>
              <span v-else class="role-empty">—</span>
            </div>

            <!-- Actions -->
            <div class="comp-actions">
              <button
                class="btn-xs learn-btn"
                :class="{ active: learningLabel === comp.label }"
                @click="startLearnFor(comp.label)"
              >Learn</button>
              <button class="btn-xs" @click="openAssign(comp)">Map</button>
              <button class="btn-xs danger" @click="deleteComponent(comp.label)" title="Remove">✕</button>
            </div>
          </div>

          <!-- New component via learn -->
          <div class="comp-row new-row">
            <span class="comp-icon">+</span>
            <div class="comp-info"><span class="comp-label muted">New component via learn…</span></div>
            <div class="comp-role"></div>
            <div class="comp-actions">
              <button class="btn-xs learn-btn" :class="{ active: learningLabel === '__new__' }" @click="startLearnFor('__new__')">Learn</button>
            </div>
          </div>
        </div>
      </section>

      <!-- ══════════════════════════════════════════════
           RIGHT: mapping targets
           ══════════════════════════════════════════════ -->
      <section class="mp-section targets-section">

        <!-- Nav actions -->
        <div class="section-header">
          <span class="section-title">Navigation</span>
        </div>
        <div class="target-list">
          <div v-for="(cfg, key) in navActions" :key="key" class="target-row">
            <span class="target-role">nav.{{ key }}</span>
            <span class="target-arrow">→</span>
            <span class="target-desc">{{ cfg.action }}</span>
            <span class="target-bound" :class="{ bound: getBoundComponentForRole('nav.' + key) }">
              {{ getBoundComponentForRole('nav.' + key) || '—' }}
            </span>
          </div>
        </div>

        <!-- Page shortcuts -->
        <div class="section-header" style="margin-top:12px">
          <span class="section-title">Page Shortcuts</span>
          <button class="btn-xs" @click="addPageGoto">+ Add</button>
        </div>
        <div class="target-list">
          <div v-for="(entry, idx) in pageGotoList" :key="idx" class="target-row">
            <span class="target-role">nav.pageGoto.{{ idx }}</span>
            <span class="target-arrow">→</span>
            <select class="target-select" :value="entry" @change="pageGotoList[idx] = $event.target.value">
              <option value="">— select page —</option>
              <option v-for="pg in pages" :key="pg" :value="pg">{{ pg }}</option>
            </select>
            <span class="target-bound" :class="{ bound: getBoundComponentForRole('nav.pageGoto.' + idx) }">
              {{ getBoundComponentForRole('nav.pageGoto.' + idx) || '—' }}
            </span>
            <button class="btn-xs danger" @click="removePageGoto(idx)">✕</button>
          </div>
          <div v-if="!pageGotoList.length" class="empty-hint">No shortcuts yet.</div>
        </div>

        <!-- Fixed OSC targets — grouped by server -->
        <div v-for="(group, srvId) in fixedByServer" :key="srvId">
          <div class="section-header" style="margin-top:12px">
            <span class="section-title">Fixed OSC — {{ serverName(Number(srvId)) }}</span>
            <button class="btn-xs" @click="addFixed(Number(srvId))">+ Add</button>
          </div>
          <div class="target-list">
            <div v-for="(fm, key) in group" :key="key" class="target-row fixed-row">
              <!-- Role name (double-click to rename) -->
              <template v-if="editingFixedKey === key">
                <input
                  class="label-input fixed-key-input"
                  :value="key"
                  @blur="finishFixedKeyEdit(key, $event.target.value)"
                  @keydown.enter="finishFixedKeyEdit(key, $event.target.value)"
                  @keydown.escape="editingFixedKey = null"
                  autofocus
                />
              </template>
              <span v-else class="target-role" @dblclick="editingFixedKey = key" title="Double-click to rename">
                fixed.{{ key }}
              </span>

              <span class="target-arrow">→</span>

              <div class="fixed-editor">
                <input class="fixed-path" :value="fm.path" placeholder="/osc/path" @input="fm.path = $event.target.value" />
                <select class="fixed-mode" :value="fm.mode" @change="fm.mode = $event.target.value">
                  <option value="absolute">absolute</option>
                  <option value="delta">delta</option>
                </select>
              </div>

              <span class="target-bound" :class="{ bound: getBoundComponentForRole('fixed.' + key) }">
                {{ getBoundComponentForRole('fixed.' + key) || '—' }}
              </span>

              <button class="btn-xs danger" @click="removeFixed(key)">✕</button>
            </div>
          </div>
        </div>

        <!-- Add fixed for a server that has none yet -->
        <div class="section-header" style="margin-top:12px" v-if="servers.length > fixedServerCount">
          <button class="btn-xs" @click="addFixedToNewServer">+ Fixed mapping for other server</button>
        </div>

      </section>
    </div>

    <!-- ══════════════════════════════════════════════
         Assign modal
         ══════════════════════════════════════════════ -->
    <div v-if="assignModal" class="modal-backdrop" @click.self="closeAssign">
      <div class="modal">
        <div class="modal-title">
          Map "{{ assignModal.label }}"
          <span v-if="learnResult.type" class="type-badge" :class="learnResult.type" style="margin-left:8px">
            {{ learnResult.type?.toUpperCase() }}
          </span>
        </div>

        <div class="modal-body">

          <!-- Post-learn: interface + CMT type pickers -->
          <template v-if="learnResult.ready">
            <div class="picker-row">
              <label class="picker-label">Interface type</label>
              <div class="picker-options">
                <button
                  v-for="it in interfaceTypeOptions"
                  :key="it.value"
                  class="picker-btn"
                  :class="{ active: assignInterfaceType === it.value }"
                  @click="assignInterfaceType = it.value; assignCMT = suggestCMT(learnResult.type, it.value)"
                >{{ it.icon }} {{ it.label }}</button>
              </div>
            </div>
            <div class="picker-row">
              <label class="picker-label">Message type</label>
              <div class="picker-options">
                <button
                  v-for="ct in cmtOptions(learnResult.type)"
                  :key="ct.value"
                  class="picker-btn"
                  :class="{ active: assignCMT === ct.value }"
                  @click="assignCMT = ct.value"
                >{{ ct.label }}</button>
              </div>
            </div>
            <div class="picker-divider" />
          </template>

          <div class="modal-hint">Assign a role:</div>

          <div class="role-group">
            <div class="role-group-title">Navigation</div>
            <div
              v-for="(cfg, key) in navActions" :key="key"
              class="role-option" :class="{ selected: assignRole === 'nav.' + key }"
              @click="assignRole = 'nav.' + key"
            >nav.{{ key }}</div>
            <div
              v-for="(entry, idx) in pageGotoList" :key="'pg' + idx"
              class="role-option" :class="{ selected: assignRole === 'nav.pageGoto.' + idx }"
              @click="assignRole = 'nav.pageGoto.' + idx"
            >nav.pageGoto.{{ idx }} → {{ entry || '(unset)' }}</div>
          </div>

          <div class="role-group">
            <div class="role-group-title">Fixed OSC</div>
            <div
              v-for="(fm, key) in fixedMappings" :key="key"
              class="role-option" :class="{ selected: assignRole === 'fixed.' + key }"
              @click="assignRole = 'fixed.' + key"
            >fixed.{{ key }} <span class="role-path">{{ fm.path }}</span></div>
          </div>

          <div class="role-group">
            <div class="role-group-title">Parameter Bank slot</div>
            <div class="param-grid">
              <div
                v-for="n in 16" :key="n"
                class="role-option param-opt"
                :class="{ selected: assignRole === 'param.' + n + '.fader' }"
                @click="assignRole = 'param.' + n + '.fader'"
              >{{ n }}</div>
            </div>
          </div>

          <div class="role-group">
            <div class="role-group-title">Custom / clear</div>
            <input class="fixed-path" v-model="assignRole" placeholder="type a role string…" />
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
import { ref, reactive, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { apiClient } from '../api.js'

const props = defineProps({
  config: { type: Object, default: () => null }
})

// ── State ──────────────────────────────────────────────────────────
const loading = ref(true)
const saving  = ref(false)
const error   = ref(null)

const profile       = ref({})
const navActions    = reactive({})
const pageGotoList  = reactive([])
const fixedMappings = reactive({})   // key → { path, serverId, mode }
const pages         = ref([])

const editingLabel    = ref(null)
const editingFixedKey = ref(null)
const labelInputRef   = ref(null)

// MIDI learn
const learnActive      = ref(false)
const learnReady       = ref(false)
const learnResult      = ref({})
const learnMessages    = ref([])   // rolling log from server
const selectedLearnMsg = ref(-1)   // index into learnMessages that user clicked
const learningLabel    = ref(null)
let learnPollTimer     = null

function selectLearnMsg(idx) {
  selectedLearnMsg.value = idx
  const msg = learnMessages.value[idx]
  learnResult.value = { ...msg, ready: true }
  learnReady.value = true

  // Auto-open assign modal
  if (learningLabel.value === '__new__') {
    const label = prompt('Label for new component:')?.trim()
    if (!label) return
    const newComp = {
      label,
      channel:       msg.channel,
      address:       msg.type === 'note' ? msg.pitch : msg.control,
      type:          msg.type === 'pitch_bend' ? 'pitch_bend' : msg.type === 'note' ? 'note_toggle' : 'cc',
      interfaceType: 'button'
    }
    if (!profile.value.components) profile.value.components = []
    profile.value.components.push(newComp)
    clearInterval(learnPollTimer)
    assignModal.value         = newComp
    assignRole.value          = ''
    assignInterfaceType.value = 'button'
    assignCMT.value           = suggestCMT(msg.type, 'button')
  } else if (learningLabel.value) {
    const comp = components.value.find(c => c.label === learningLabel.value)
    if (comp) {
      clearInterval(learnPollTimer)
      assignModal.value         = comp
      assignRole.value          = getRoleForLabel(comp.label) || ''
      assignInterfaceType.value = comp.interfaceType || 'button'
      assignCMT.value           = suggestCMT(msg.type, assignInterfaceType.value)
    }
  }
}

// Assign modal
const assignModal         = ref(null)
const assignRole          = ref('')
const assignInterfaceType = ref('button')
const assignCMT           = ref('note_toggle')

const servers = computed(() => props.config?.servers || [])

// ── Lookup helpers ─────────────────────────────────────────────────
const components = computed(() => {
  const arr = profile.value?.components
  return Array.isArray(arr) ? arr : []
})
const bindings = computed(() => {
  const b = profile.value?.bindings
  return b && typeof b === 'object' ? b : {}
})

function getRoleForLabel(label) {
  for (const [role, lbl] of Object.entries(bindings.value))
    if (lbl === label) return role
  return null
}
function getBoundComponentForRole(role) {
  return bindings.value[role] || null
}

// Fixed mappings grouped by serverId
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

function serverName(idx) {
  return servers.value[idx]?.id || `Server ${idx}`
}

// ── Interface/CMT helpers ──────────────────────────────────────────
const interfaceTypeOptions = [
  { value: 'button',    label: 'Button',     icon: '□' },
  { value: 'button_lp', label: 'Button LP',  icon: '■' },
  { value: 'knob',      label: 'Knob',       icon: '◎' },
  { value: 'fader',     label: 'Fader',      icon: '▮' },
]

function interfaceIcon(type) {
  const m = { knob: '◎', fader: '▮', button: '□', button_lp: '■' }
  return m[type] || '·'
}

const CMT_OPTIONS = {
  cc: [
    { value: 'cc',                    label: 'CC (absolute)' },
    { value: 'cc_toggle',             label: 'CC Toggle' },
    { value: 'encoder',               label: 'Encoder (absolute)' },
    { value: 'encoder_relative',      label: 'Encoder (relative)' },
  ],
  note: [
    { value: 'note',                  label: 'Note (momentary)' },
    { value: 'note_toggle',           label: 'Note Toggle' },
  ],
  pitch_bend: [
    { value: 'pitch_bend',            label: 'Pitch Bend' },
  ],
}

function cmtOptions(detectedType) {
  return CMT_OPTIONS[detectedType] || CMT_OPTIONS.cc
}

function suggestCMT(detectedType, interfaceType) {
  if (detectedType === 'note')       return 'note_toggle'
  if (detectedType === 'pitch_bend') return 'pitch_bend'
  if (interfaceType === 'knob')      return 'encoder_relative'
  if (interfaceType === 'fader')     return 'cc'
  return 'cc_toggle'
}

// ── Load ───────────────────────────────────────────────────────────
async function reload() {
  loading.value = true; error.value = null
  try {
    const [prof, maps] = await Promise.all([apiClient.fetchProfile(), apiClient.fetchMappings()])
    profile.value = prof || {}

    Object.keys(navActions).forEach(k => delete navActions[k])
    if (maps.nav && typeof maps.nav === 'object') {
      for (const [k, v] of Object.entries(maps.nav))
        if (k !== 'pageGoto') navActions[k] = v
    }

    pageGotoList.length = 0
    if (Array.isArray(maps.nav?.pageGoto)) maps.nav.pageGoto.forEach(e => pageGotoList.push(e))

    Object.keys(fixedMappings).forEach(k => delete fixedMappings[k])
    if (maps.fixed && typeof maps.fixed === 'object') {
      for (const [k, v] of Object.entries(maps.fixed))
        fixedMappings[k] = reactive({ path: v.path || '', serverId: v.serverId ?? 0, mode: v.mode || 'absolute' })
    }

    pages.value = Array.isArray(maps.pages) ? maps.pages : []
  } catch (e) {
    error.value = e.message || 'Failed to load'
  } finally {
    loading.value = false
  }
}

// ── Save ───────────────────────────────────────────────────────────
async function saveAll() {
  saving.value = true
  try {
    const payload = {
      nav: { ...Object.fromEntries(Object.entries(navActions)), pageGoto: [...pageGotoList] },
      fixed: Object.fromEntries(
        Object.entries(fixedMappings).map(([k, v]) => [k, { path: v.path, serverId: v.serverId, mode: v.mode }])
      )
    }
    await apiClient.saveMappings(payload)
    await apiClient.saveProfile({ ...profile.value })
  } catch (e) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

// ── Component editing ──────────────────────────────────────────────
function startLabelEdit(label) {
  editingLabel.value = label
  nextTick(() => labelInputRef.value?.focus())
}

function finishLabelEdit(comp, newLabel) {
  const trimmed = newLabel.trim()
  if (trimmed && trimmed !== comp.label) {
    // Rename in bindings too
    const oldRole = getRoleForLabel(comp.label)
    if (oldRole && profile.value.bindings) {
      profile.value.bindings[oldRole] = trimmed
    }
    comp.label = trimmed
  }
  editingLabel.value = null
}

function deleteComponent(label) {
  if (!profile.value.components) return
  const idx = profile.value.components.findIndex(c => c.label === label)
  if (idx >= 0) {
    profile.value.components.splice(idx, 1)
    // Remove binding too
    if (profile.value.bindings) {
      for (const [role, lbl] of Object.entries(profile.value.bindings)) {
        if (lbl === label) delete profile.value.bindings[role]
      }
    }
  }
}

// ── Fixed mapping CRUD ─────────────────────────────────────────────
function addFixed(serverId = 0) {
  const key = prompt('Role name (without "fixed." prefix):')
  if (!key) return
  fixedMappings[key] = reactive({ path: '', serverId, mode: 'absolute' })
}

function addFixedToNewServer() {
  const srvOptions = servers.value.map((s, i) => `${i}: ${s.id}`).join('\n')
  const idx = parseInt(prompt(`Server index:\n${srvOptions}`), 10)
  if (isNaN(idx)) return
  addFixed(idx)
}

function removeFixed(key) { delete fixedMappings[key] }

function finishFixedKeyEdit(oldKey, newKey) {
  const trimmed = newKey.trim()
  if (trimmed && trimmed !== oldKey) {
    const val = fixedMappings[oldKey]
    delete fixedMappings[oldKey]
    fixedMappings[trimmed] = val
    // Update binding in profile
    if (profile.value.bindings) {
      const oldRole = 'fixed.' + oldKey
      const comp = profile.value.bindings[oldRole]
      if (comp) {
        delete profile.value.bindings[oldRole]
        profile.value.bindings['fixed.' + trimmed] = comp
      }
    }
  }
  editingFixedKey.value = null
}

// ── Page goto CRUD ─────────────────────────────────────────────────
function addPageGoto()       { pageGotoList.push('') }
function removePageGoto(idx) { pageGotoList.splice(idx, 1) }

// ── MIDI Learn ─────────────────────────────────────────────────────
async function startLearnFor(label) {
  learningLabel.value    = label
  learnReady.value       = false
  learnResult.value      = {}
  learnMessages.value    = []
  selectedLearnMsg.value = -1
  await apiClient.learnStart()
  learnActive.value = true
  startLearnPoll()
}

async function stopLearn() {
  await apiClient.learnStop()
  learnActive.value = false
  clearInterval(learnPollTimer)
  // Keep messages visible so user can still pick
}

function cancelLearn() {
  learnReady.value    = false
  learnResult.value   = {}
  learnMessages.value = []
  selectedLearnMsg.value = -1
  learningLabel.value = null
  learnActive.value   = false
}

function startLearnPoll() {
  clearInterval(learnPollTimer)
  learnPollTimer = setInterval(async () => {
    try {
      const s = await apiClient.learnStatus()
      learnActive.value = s.active
      if (Array.isArray(s.messages)) learnMessages.value = s.messages

      // If the first message has arrived and we haven't yet auto-selected, pick it
      if (s.ready && !learnReady.value) {
        learnReady.value  = true
        learnResult.value = s
        selectedLearnMsg.value = s.messages ? s.messages.length - 1 : 0
      }
    } catch (_) {}
  }, 400)
}

// ── Assign modal ───────────────────────────────────────────────────
function openAssign(comp) {
  assignModal.value       = comp
  assignRole.value        = getRoleForLabel(comp.label) || ''
  assignInterfaceType.value = comp.interfaceType || 'button'
  assignCMT.value         = comp.type || 'note_toggle'
}

function openAssignWithLearn(comp, learnData) {
  assignModal.value = comp
  assignRole.value  = getRoleForLabel(comp.label) || ''
  assignInterfaceType.value = comp.interfaceType || 'button'
  assignCMT.value   = suggestCMT(learnData.type, assignInterfaceType.value)
}

function closeAssign() {
  assignModal.value = null
  // Resume polling if learn is still active so user can pick a different message
  if (learnActive.value || learnMessages.value.length > 0) startLearnPoll()
}

async function confirmAssign() {
  if (!assignModal.value) return
  const comp  = assignModal.value
  const role  = assignRole.value

  // Update component in memory from learned data
  if (learnResult.value.ready) {
    const lr = learnResult.value
    comp.interfaceType = assignInterfaceType.value
    comp.type          = assignCMT.value
    comp.channel       = lr.channel || comp.channel
    // address is pitch for note messages, control number for CC/pitch_bend
    comp.address       = lr.type === 'note' ? lr.pitch : lr.control
  }

  // Update bindings
  if (!profile.value.bindings) profile.value.bindings = {}
  for (const [k, v] of Object.entries(profile.value.bindings))
    if (v === comp.label) delete profile.value.bindings[k]
  if (role) profile.value.bindings[role] = comp.label

  // Persist immediately via learnAssign if came from hardware
  if (learnResult.value?.ready) {
    const lr = learnResult.value
    await apiClient.learnAssign({
      label:         comp.label,
      channel:       lr.channel || comp.channel,
      address:       lr.type === 'note' ? lr.pitch : lr.control,
      type:          assignCMT.value,
      interfaceType: assignInterfaceType.value,
      role
    })
  }

  closeAssign()
  // End learn mode — user confirmed, we're done
  cancelLearn()
  await apiClient.learnStop()
}

onMounted(() => { reload() })
onUnmounted(() => clearInterval(learnPollTimer))
</script>

<style scoped>
.mapping-panel {
  display: flex; flex-direction: column; height: 100%; overflow: hidden;
  background: var(--bg-panel); position: relative;
}

/* ── Header ── */
.mp-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 16px; border-bottom: 1px solid var(--border-strong);
  background: var(--bg-shell); flex-shrink: 0;
}
.mp-title { font-size: 13px; font-weight: 700; color: var(--text-main); }
.mp-header-actions { display: flex; gap: 8px; }
.mp-state { padding: 24px; color: var(--text-muted); font-size: 12px; }
.mp-state.error { color: #f44; }

/* ── Body ── */
.mp-body { display: flex; flex: 1; overflow: hidden; }
.mp-section {
  flex: 1; display: flex; flex-direction: column; overflow: hidden;
  border-right: 1px solid var(--border-strong);
}
.mp-section:last-child { border-right: none; }
.targets-section { overflow-y: auto; }

/* ── Section headers ── */
.section-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 7px 12px; border-bottom: 1px solid var(--border-soft);
  flex-shrink: 0; background: var(--bg-panel-soft);
}
.section-title { font-size: 10px; font-weight: 700; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.06em; }
.section-sub   { font-size: 10px; color: var(--text-muted); }

/* ── Learn bar ── */
.learn-bar {
  display: flex; flex-direction: column; gap: 4px;
  padding: 6px 12px; border-bottom: 1px solid var(--border-soft);
  font-size: 11px; color: var(--text-muted); flex-shrink: 0;
}
.learn-bar.active { background: #131f13; border-color: #2d4d2d; }
.learn-bar-top { display: flex; align-items: center; gap: 8px; }
.learn-hint  { font-style: italic; }
.learn-pulse { color: #4caf50; animation: pulse 1s infinite; }
.learn-result { color: #80d8ff; }

/* Live MIDI message log */
.learn-msg-log {
  display: flex; flex-direction: column; gap: 2px;
  max-height: 120px; overflow-y: auto;
  border-top: 1px solid var(--border-soft);
  padding-top: 4px; margin-top: 2px;
}
.learn-msg-row {
  display: flex; align-items: center; gap: 8px;
  padding: 3px 6px; border-radius: 3px; cursor: pointer;
  border: 1px solid transparent;
}
.learn-msg-row:hover    { background: var(--bg-hover); }
.learn-msg-row.selected { background: var(--bg-active); border-color: var(--accent-dim); }
.msg-detail { font-size: 11px; color: var(--text-muted); font-family: monospace; }

/* ── Type badge ── */
.type-badge {
  padding: 1px 6px; border-radius: 3px; font-size: 9px; font-weight: 800;
  letter-spacing: 0.08em; flex-shrink: 0;
}
.type-badge.cc         { background: #1a3a5a; color: #80d8ff; border: 1px solid #2d6a9a; }
.type-badge.note       { background: #3a1a5a; color: #d0a0ff; border: 1px solid #6a2d9a; }
.type-badge.pitch_bend { background: #3a2a1a; color: #ffcc80; border: 1px solid #9a6a2d; }

/* ── Component list ── */
.comp-list { flex: 1; overflow-y: auto; }
.comp-row {
  display: flex; align-items: center; gap: 8px;
  padding: 5px 12px; border-bottom: 1px solid var(--border-soft); font-size: 11px;
}
.comp-row:hover { background: var(--bg-hover); }
.comp-row.is-learning { background: #131f13; }
.comp-row.has-role .comp-label { color: var(--text-main); }
.comp-row.new-row { color: var(--text-dim); border-top: 1px dashed var(--border-soft); }
.comp-icon  { width: 16px; text-align: center; font-size: 13px; color: var(--text-dim); flex-shrink: 0; }
.comp-info  { flex: 1; min-width: 0; }
.comp-label { display: block; font-weight: 600; color: var(--text-muted); cursor: default; }
.comp-label:hover { color: var(--text-main); text-decoration: underline dotted; }
.comp-detail{ display: block; font-size: 10px; color: var(--text-dim); font-family: monospace; }
.comp-role  { width: 130px; flex-shrink: 0; }
.role-badge { background: var(--bg-active); border: 1px solid var(--accent-dim); border-radius: 3px; padding: 1px 5px; color: var(--accent); font-size: 10px; }
.role-empty { color: var(--text-dim); font-size: 10px; }
.comp-actions { display: flex; gap: 4px; flex-shrink: 0; }
.muted { color: var(--text-dim) !important; }

/* Inline label edit */
.label-input {
  width: 100%; background: var(--bg-panel-soft); border: 1px solid var(--accent-dim);
  border-radius: 3px; color: var(--text-main); font-size: 11px; font-weight: 600;
  padding: 1px 4px; outline: none;
}

/* ── Target list ── */
.target-list { padding: 0; }
.target-row {
  display: flex; align-items: center; gap: 6px;
  padding: 5px 12px; border-bottom: 1px solid var(--border-soft); font-size: 11px;
}
.target-row:hover { background: var(--bg-hover); }
.target-role  { width: 150px; flex-shrink: 0; font-family: monospace; font-size: 10px; color: var(--text-muted); cursor: default; }
.target-role:hover { color: var(--text-main); text-decoration: underline dotted; }
.target-arrow { color: var(--text-dim); flex-shrink: 0; }
.target-desc  { flex: 1; color: var(--text-dim); }
.target-bound { width: 90px; flex-shrink: 0; font-size: 10px; color: var(--text-dim); text-align: right; font-family: monospace; }
.target-bound.bound { color: var(--accent); }
.target-select { flex: 1; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 11px; padding: 2px 4px; }
.empty-hint { padding: 8px 12px; color: var(--text-dim); font-size: 10px; font-style: italic; }

.fixed-row { flex-wrap: nowrap; }
.fixed-editor { flex: 1; display: flex; gap: 4px; min-width: 0; }
.fixed-path { flex: 1; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 11px; padding: 2px 5px; font-family: monospace; min-width: 0; }
.fixed-mode { width: 76px; flex-shrink: 0; background: var(--bg-panel-soft); border: 1px solid var(--border-soft); border-radius: 3px; color: var(--text-main); font-size: 11px; padding: 2px 4px; }
.fixed-key-input { width: 120px; background: var(--bg-panel-soft); border: 1px solid var(--accent-dim); border-radius: 3px; color: var(--text-main); font-size: 10px; font-family: monospace; padding: 1px 4px; }

/* ── Buttons ── */
.btn-primary, .btn-secondary, .btn-xs {
  border-radius: 4px; font-size: 11px; font-weight: 600; cursor: pointer;
  padding: 4px 10px; border: 1px solid transparent; transition: background 0.1s;
}
.btn-primary   { background: var(--accent); color: #000; border-color: var(--accent); }
.btn-primary:hover   { opacity: 0.85; }
.btn-primary:disabled { opacity: 0.4; cursor: default; }
.btn-secondary { background: var(--bg-panel-soft); color: var(--text-main); border-color: var(--border-strong); }
.btn-secondary:hover { background: var(--bg-hover); }
.btn-secondary:disabled { opacity: 0.4; cursor: default; }
.btn-xs { padding: 2px 7px; font-size: 10px; background: var(--bg-panel-soft); color: var(--text-muted); border-color: var(--border-soft); border-radius: 3px; }
.btn-xs:hover { background: var(--bg-hover); color: var(--text-main); }
.btn-xs.learn-btn.active { background: #1f3a1f; color: #4caf50; border-color: #3a6b3a; }
.btn-xs.danger:hover { background: #4d1a1a; color: #f44; border-color: #6b2d2d; }

/* ── Modal ── */
.modal-backdrop { position: absolute; inset: 0; background: rgba(0,0,0,0.65); display: flex; align-items: center; justify-content: center; z-index: 100; }
.modal { background: var(--bg-shell); border: 1px solid var(--border-strong); border-radius: 6px; width: 460px; max-height: 82vh; display: flex; flex-direction: column; overflow: hidden; }
.modal-title { padding: 12px 16px; font-size: 13px; font-weight: 700; color: var(--text-main); border-bottom: 1px solid var(--border-strong); flex-shrink: 0; display: flex; align-items: center; }
.modal-body  { flex: 1; overflow-y: auto; padding: 12px 16px; display: flex; flex-direction: column; gap: 10px; }
.modal-hint  { font-size: 11px; color: var(--text-muted); }
.modal-footer { display: flex; justify-content: flex-end; gap: 8px; padding: 10px 16px; border-top: 1px solid var(--border-strong); flex-shrink: 0; }

/* ── Interface/CMT pickers ── */
.picker-row   { display: flex; align-items: center; gap: 10px; }
.picker-label { width: 100px; flex-shrink: 0; font-size: 10px; font-weight: 700; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.06em; }
.picker-options { display: flex; gap: 4px; flex-wrap: wrap; }
.picker-btn {
  padding: 3px 10px; font-size: 11px; border-radius: 4px; cursor: pointer;
  background: var(--bg-panel-soft); border: 1px solid var(--border-soft);
  color: var(--text-muted); font-weight: 500;
}
.picker-btn:hover  { background: var(--bg-hover); color: var(--text-main); }
.picker-btn.active { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.picker-divider { border-top: 1px solid var(--border-soft); margin: 2px 0; }

/* ── Role options ── */
.role-group { display: flex; flex-direction: column; gap: 2px; }
.role-group-title { font-size: 10px; font-weight: 700; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.06em; margin-bottom: 2px; }
.role-option { padding: 4px 8px; border-radius: 3px; cursor: pointer; font-size: 11px; color: var(--text-muted); font-family: monospace; border: 1px solid transparent; }
.role-option:hover   { background: var(--bg-hover); color: var(--text-main); }
.role-option.selected { background: var(--bg-active); border-color: var(--accent-dim); color: var(--accent); }
.role-path { color: var(--text-dim); font-size: 10px; margin-left: 6px; }
.param-grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 3px; }
.param-opt  { text-align: center; padding: 3px 4px; }

@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
</style>
