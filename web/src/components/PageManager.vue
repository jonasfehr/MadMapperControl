<template>
  <div class="page-manager">
    <div class="manager-tabs">
      <button
        v-for="(tab, idx) in tabs"
        :key="idx"
        class="tab"
        :class="{ active: activeTab === idx }"
        @click="activeTab = idx"
      >
        {{ tab === 'editor' ? 'Editor' : 'Builder' }}
      </button>
    </div>

    <!-- Tab 0: Page Builder -->
    <div v-if="activeTab === 0" class="page-builder" :class="{ 'has-selection': selectedPageIdx !== null }">

      <!-- Left: page list -->
      <div class="builder-section">
        <div class="section-header">
          Pages
          <div class="header-btns">
            <button class="header-icon-btn" @click="exportPages" title="Export pages">↧</button>
            <button class="header-icon-btn" @click="triggerImport" title="Import pages">↥</button>
            <button class="header-icon-btn" @click="addPage" title="New page">+</button>
          </div>
        </div>
        <div class="pages-list">
          <div
            v-for="(page, idx) in pages.pages"
            :key="idx"
            class="page-item"
            :class="{ selected: selectedPageIdx === idx, 'drag-over': dragOverIdx === idx, dragging: dragSrcIdx === idx }"
            draggable="true"
            @click="selectPage(idx)"
            @dragstart="onDragStart(idx, $event)"
            @dragover.prevent="onDragOver(idx)"
            @dragleave="onDragLeave"
            @drop.prevent="onDrop(idx)"
            @dragend="onDragEnd"
          >
            <span class="page-swatch page-drag-handle"></span>
            <input
              v-if="editingPageIdx === idx"
              class="page-rename-input"
              v-model="editingName"
              @blur="commitRename"
              @keyup.enter="commitRename"
              @keyup.escape="cancelRename"
              @click.stop
            />
            <span v-else class="page-name" @dblclick.stop="startRename(idx)">{{ page.name || 'Unnamed' }}</span>
            <span v-show="editingPageIdx !== idx" class="page-count">{{ resolvedParameterCount(page) }}</span>
            <button v-show="editingPageIdx !== idx" class="delete-btn" @click.stop="deletePage(idx)">✕</button>
          </div>
        </div>
      </div>

      <!-- Center: configurator -->
      <div v-if="selectedPageIdx !== null" class="builder-section parameters">
        <div class="section-header">Configurator</div>

        <div class="add-parameters">
          <div class="server-row">
            <span class="server-label">Server</span>
            <select v-model="selectedPageServerId" @change="setSelectedPageServer">
              <option v-for="server in availableServerOptions" :key="server.id" :value="server.id">
                {{ server.name }}
              </option>
            </select>
          </div>

          <div class="filter-builder">
            <h4>Filter Line Builder</h4>
            <div class="selected-path">
              <button class="crumb" @click="resetPathSelection">/</button>
              <button
                v-for="(segment, idx) in selectedPathSegments"
                :key="`${segment}-${idx}`"
                class="crumb"
                @click="truncatePathSelection(idx)"
              >
                {{ segment }}
              </button>
            </div>
            <div class="next-options">
              <button v-if="selectedPathSegments.length" class="chip chip--wildcard" @click="insertWildcardSegment" title="Wildcard segment">*</button>
              <button v-for="option in nextPathOptions" :key="option" class="chip" @click="selectNextPathOption(option)">
                {{ option }}
              </button>
            </div>
            <div class="filter-search">
              <input v-model="manualElementFilter" type="text" placeholder="e.g. /project_waves/Custom/*" />
            </div>
            <div class="builder-actions">
              <button class="mini-btn secondary" @click="useSelectedPathAsFilter">Use Path</button>
              <button class="mini-btn secondary" @click="useSelectedPathAsPrefix">Add *</button>
              <button class="mini-btn" @click="addManualFilterLine">Add Filter</button>
              <button class="mini-btn secondary" @click="clearManualFilterLine">Clear</button>
            </div>
          </div>

          <div class="param-selector">
            <div v-for="param in filteredAvailableParams" :key="param.key" class="param-option">
              <span class="param-label" @click="addParameterToPage(param)">+ {{ param.path }}</span>
            </div>
            <p v-if="!filteredAvailableParams.length" class="empty">No parameters available</p>
          </div>
        </div>

        <div class="current-parameters">
          <h4>Current Parameters</h4>
          <div class="param-list">
            <div v-for="(item, idx) in currentPageParams" :key="`${item.serverId}::${item.path}::${idx}`" class="param-item">
              <span class="param-srv" :title="`Server ${item.serverId}`">{{ item.serverId }}</span>
              <span class="param-path">{{ item.path }}</span>
              <button class="remove-btn" @click="removeParameterFromPage(idx)">✕</button>
            </div>
            <p v-if="!currentPageParams.length" class="empty">No parameters yet</p>
          </div>
        </div>
      </div>

      <!-- Right: resolved parameters -->
      <div v-if="selectedPageIdx !== null" class="builder-section resolved-col">
        <div class="section-header">
          Resolved
          <span class="count-badge">{{ resolvedParameters.length }}</span>
        </div>
        <div class="resolved-list">
          <div v-for="item in resolvedParameters" :key="`${item.serverId}::${item.path}`" class="resolved-item">
            <span class="resolved-srv" :title="item.serverName">{{ item.serverId }}</span>
            <span class="resolved-path">{{ item.path }}</span>
          </div>
          <p v-if="!resolvedParameters.length" class="empty">No matches yet</p>
        </div>
      </div>
    </div>

    <!-- Tab 1: JSON Editor -->
    <div v-if="activeTab === 1" class="json-editor-section">
      <div class="json-editor">
        <textarea v-model="editedJson" placeholder="Edit pages JSON here..." @input="onJsonChange"></textarea>
      </div>
      <div class="validation-message" :class="{ error: jsonError }">
        {{ jsonError || 'JSON is valid' }}
      </div>
      <div class="button-group">
        <button class="save-btn" @click="handleJsonSave" :disabled="!!jsonError">💾 Save Changes</button>
        <button class="reset-btn" @click="handleReset">↶ Undo</button>
      </div>
    </div>

    <div v-if="saveStatus" class="save-status" :class="saveStatus.type">{{ saveStatus.message }}</div>

    <!-- Hidden file input for import -->
    <input ref="fileInputEl" type="file" accept=".json" style="display:none" @change="handleImportFile" />

    <!-- Import overlay -->
    <div v-if="importDraft" class="import-overlay" @click.self="cancelImport">
      <div class="import-panel">
        <div class="section-header">
          Import Pages
          <button class="header-icon-btn" @click="cancelImport">✕</button>
        </div>

        <div class="import-mode-row">
          <label class="mode-option">
            <input type="radio" v-model="importMode" value="merge" />
            <span>Merge with existing</span>
          </label>
          <label class="mode-option">
            <input type="radio" v-model="importMode" value="replace" />
            <span>Replace all</span>
          </label>
        </div>

        <div class="import-list">
          <label
            v-for="(page, idx) in importDraft.pages"
            :key="idx"
            class="import-row"
            :class="{ checked: importSelections.has(idx) }"
          >
            <span class="import-swatch"></span>
            <input type="checkbox" :checked="importSelections.has(idx)" @change="toggleImportPage(idx)" />
            <span class="import-name">{{ page.name || 'Unnamed' }}</span>
            <span class="import-count">{{ page.elements?.length || 0 }} filters</span>
          </label>
          <p v-if="!importDraft.pages?.length" class="empty">No pages found in file</p>
        </div>

        <div class="import-footer">
          <button class="mini-btn secondary" @click="cancelImport">Cancel</button>
          <button class="mini-btn" @click="selectAllImport">All</button>
          <button class="mini-btn secondary" @click="selectNoneImport">None</button>
          <button
            class="mini-btn import-confirm-btn"
            @click="confirmImport"
            :disabled="importSelections.size === 0"
          >
            Import {{ importSelections.size }} page{{ importSelections.size === 1 ? '' : 's' }}
          </button>
        </div>
      </div>
    </div>

  </div>
</template>

<script>
import { ref, computed, watch, nextTick } from 'vue'

export default {
  name: 'PageManager',
  props: {
    pages: {
      type: Object,
      default: () => ({ pages: [], subpages: [] })
    },
    allParameters: {
      type: Array,
      default: () => []
    }
  },
  emits: ['save', 'activate-page'],
  setup(props, { emit }) {
    const activeTab = ref(0)
    const selectedPageIdx = ref(null)
    const editingPageIdx = ref(null)
    const editingName = ref('')
    const selectedPageServerId = ref('server_0')
    const selectedPathSegments = ref([])
    const manualElementFilter = ref('')
    const editedJson = ref('')
    const jsonError = ref('')
    const saveStatus = ref(null)
    const originalData = ref(null)
    const fileInputEl = ref(null)
    const importDraft = ref(null)
    const importSelections = ref(new Set())
    const importMode = ref('merge')
    const dragSrcIdx = ref(null)
    const dragOverIdx = ref(null)
    let autoSaveTimer = null

    const tabs = ['builder', 'editor']

    // ── Server options ──────────────────────────────────────────────────

    const availableServerOptions = computed(() => {
      const map = new Map()
      props.allParameters.forEach(p => {
        if (!p || !p.serverId) return
        if (!map.has(p.serverId)) map.set(p.serverId, { id: p.serverId, name: p.serverName || p.serverId })
      })
      if (!map.size) map.set('server_0', { id: 'server_0', name: 'MadMapper' })
      return [...map.values()]
    })

    // ── Parameter computeds ─────────────────────────────────────────────

    // Normalize a raw element (string or object) to { path, serverId }
    function normalizeElement(el, defaultServerId = 0) {
      if (el && typeof el === 'object') return { path: el.path || '', serverId: el.serverId ?? defaultServerId }
      return { path: String(el), serverId: defaultServerId }
    }

    // Convert param.serverId "server_N" → number N
    function serverIdNum(serverIdStr) {
      return Number(String(serverIdStr).replace('server_', '')) || 0
    }

    const currentPageParams = computed(() => {
      if (selectedPageIdx.value === null) return []
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return []
      const defaultSid = typeof page.serverId === 'number' ? page.serverId : 0
      if (Array.isArray(page.elements)) {
        return page.elements.map(el => normalizeElement(el, defaultSid))
      }
      // Legacy surfaces/fixtures format
      const sid = defaultSid
      const result = []
      ;(page.surfaces || []).forEach(s       => result.push({ path: `/surfaces/${s}/*`,       serverId: sid }))
      ;(page.laser_surfaces || []).forEach(s => result.push({ path: `/laser_surfaces/${s}/*`, serverId: sid }))
      ;(page.fixtures || []).forEach(f       => result.push({ path: `/fixtures/${f}/*`,       serverId: sid }))
      ;(page.medias || []).forEach(m         => result.push({ path: `/media/${m}/*`,          serverId: sid }))
      return result
    })

    const availableParamsForServer = computed(() => {
      const currentPaths = new Set(currentPageParams.value.map(e => e.path))
      return props.allParameters
        .filter(p => p && p.serverId === selectedPageServerId.value)
        .filter(p => !currentPaths.has(p.path))
        .sort((a, b) => a.path.localeCompare(b.path))
    })

    function pathSegments(path) {
      if (!path) return []
      return path.split('/').filter(Boolean)
    }

    function startsWithSegments(path, segments) {
      const parts = pathSegments(path)
      if (segments.length > parts.length) return false
      return segments.every((seg, idx) => seg === '*' || parts[idx] === seg)
    }

    function joinSelectedSegments() {
      if (!selectedPathSegments.value.length) return ''
      return `/${selectedPathSegments.value.join('/')}`
    }

    const nextPathOptions = computed(() => {
      const options = new Set()
      const depth = selectedPathSegments.value.length
      availableParamsForServer.value.forEach(param => {
        const segments = pathSegments(param.path)
        if (segments.length <= depth) return
        if (!startsWithSegments(param.path, selectedPathSegments.value)) return
        options.add(segments[depth])
      })
      return [...options].sort((a, b) => a.localeCompare(b))
    })

    function matchesWildcardFilter(path, filter) {
      if (!filter) return true
      const filterSegments = pathSegments(filter)
      const pathParts = pathSegments(path)
      if (filterSegments.length > pathParts.length) return false
      return filterSegments.every((seg, idx) => seg === '*' || pathParts[idx] === seg)
    }

    // Returns { path, serverId }[] — works for both new (object) and legacy (string/surfaces) formats
    function pagePatterns(page) {
      if (!page) return []
      const defaultSid = typeof page.serverId === 'number' ? page.serverId : 0
      if (Array.isArray(page.elements)) return page.elements.map(el => normalizeElement(el, defaultSid))
      const patterns = []
      const sid = defaultSid
      if (Array.isArray(page.surfaces))       page.surfaces.forEach(s       => patterns.push({ path: `/surfaces/${s}/*`,       serverId: sid }))
      if (Array.isArray(page.laser_surfaces)) page.laser_surfaces.forEach(s => patterns.push({ path: `/laser_surfaces/${s}/*`, serverId: sid }))
      if (Array.isArray(page.fixtures))       page.fixtures.forEach(f       => patterns.push({ path: `/fixtures/${f}/*`,       serverId: sid }))
      if (Array.isArray(page.medias))         page.medias.forEach(m         => patterns.push({ path: `/media/${m}/*`,          serverId: sid }))
      return patterns
    }

    // Group { path, serverId }[] patterns by server number → Map<number, string[]>
    function patternsByServer(patterns) {
      const map = new Map()
      patterns.forEach(p => {
        if (!map.has(p.serverId)) map.set(p.serverId, [])
        map.get(p.serverId).push(p.path)
      })
      return map
    }

    const resolvedParameters = computed(() => {
      if (selectedPageIdx.value === null) return []
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return []
      const byServer = patternsByServer(pagePatterns(page))
      if (!byServer.size) return []
      const seen = new Set()
      const result = []
      props.allParameters.forEach(param => {
        if (!param) return
        const sNum = serverIdNum(param.serverId)
        const patterns = byServer.get(sNum)
        if (!patterns) return
        if (patterns.some(pattern => matchesWildcardFilter(param.path, pattern))) {
          const key = `${sNum}::${param.path}`
          if (!seen.has(key)) {
            seen.add(key)
            result.push({ path: param.path, serverId: sNum, serverName: param.serverName || `server_${sNum}` })
          }
        }
      })
      return result.sort((a, b) => a.serverId - b.serverId || a.path.localeCompare(b.path))
    })

    function resolvedParameterCount(page) {
      if (!page || !props.allParameters.length) return 0
      const byServer = patternsByServer(pagePatterns(page))
      if (!byServer.size) return 0
      const resolved = new Set()
      props.allParameters.forEach(param => {
        if (!param) return
        const sNum = serverIdNum(param.serverId)
        const patterns = byServer.get(sNum)
        if (!patterns) return
        if (patterns.some(pattern => matchesWildcardFilter(param.path, pattern))) resolved.add(`${sNum}::${param.path}`)
      })
      return resolved.size
    }

    const filteredAvailableParams = computed(() => {
      let result = availableParamsForServer.value
      if (selectedPathSegments.value.length) {
        result = result.filter(param => startsWithSegments(param.path, selectedPathSegments.value))
      }
      const manualFilter = manualElementFilter.value.trim()
      if (manualFilter) {
        result = result.filter(param => matchesWildcardFilter(param.path, manualFilter))
      }
      return result
    })

    // ── Save / autosave ─────────────────────────────────────────────────

    watch(
      () => props.pages,
      (newVal) => {
        if (newVal) {
          editedJson.value = JSON.stringify(newVal, null, 2)
          originalData.value = JSON.stringify(newVal)
        }
      },
      { immediate: true, deep: true }
    )

    function buildSavePayload(liveReload = true) {
      const convertedPages = props.pages.pages.filter(page => page.name).map(page => {
        const defaultSid = typeof page.serverId === 'number' ? page.serverId : 0
        if (Array.isArray(page.elements)) {
          // Normalize all elements to {path, serverId} objects
          return {
            ...page,
            elements: page.elements.map(el => normalizeElement(el, defaultSid)),
            serverId: defaultSid,
            skipKeys: page.skipKeys || ['children']
          }
        }
        const elements = []
        const sid = defaultSid
        if (page.surfaces)       page.surfaces.forEach(s       => elements.push({ path: `/surfaces/${s}/*`,       serverId: sid }))
        if (page.laser_surfaces) page.laser_surfaces.forEach(s => elements.push({ path: `/laser_surfaces/${s}/*`, serverId: sid }))
        if (page.fixtures)       page.fixtures.forEach(f       => elements.push({ path: `/fixtures/${f}/*`,       serverId: sid }))
        if (page.medias)         page.medias.forEach(m         => elements.push({ path: `/media/${m}/*`,          serverId: sid }))
        return {
          name: page.name,
          elements: elements.length > 0 ? elements : [{ path: '/surfaces/*/opacity', serverId: sid }],
          serverId: defaultSid,
          skipKeys: page.skipKeys || ['children']
        }
      })
      return {
        pages: convertedPages,
        subpages: props.pages.subpages || [],
        liveReload,
        currentPage: selectedPageIdx.value !== null && props.pages.pages[selectedPageIdx.value]
          ? props.pages.pages[selectedPageIdx.value].name
          : ''
      }
    }

    function scheduleAutoSave(liveReload = true) {
      clearTimeout(autoSaveTimer)
      autoSaveTimer = setTimeout(() => emit('save', buildSavePayload(liveReload)), 300)
    }

    // ── Page list actions ───────────────────────────────────────────────

    // ── Drag to reorder ──────────────────────────────────────────────────

    function onDragStart(idx, event) {
      dragSrcIdx.value = idx
      event.dataTransfer.effectAllowed = 'move'
      event.dataTransfer.setData('text/plain', String(idx))
    }

    function onDragOver(idx) {
      if (dragSrcIdx.value === null || dragSrcIdx.value === idx) return
      dragOverIdx.value = idx
    }

    function onDragLeave() {
      dragOverIdx.value = null
    }

    function onDrop(targetIdx) {
      if (dragSrcIdx.value === null || dragSrcIdx.value === targetIdx) {
        dragSrcIdx.value = null
        dragOverIdx.value = null
        return
      }
      const src = dragSrcIdx.value
      const dst = targetIdx
      const pages = props.pages.pages
      const [moved] = pages.splice(src, 1)
      pages.splice(dst, 0, moved)
      // keep selectedPageIdx pointing at the same page
      if (selectedPageIdx.value === src) {
        selectedPageIdx.value = dst
      } else if (selectedPageIdx.value !== null) {
        const sel = selectedPageIdx.value
        if (src < dst && sel > src && sel <= dst) selectedPageIdx.value--
        else if (src > dst && sel >= dst && sel < src) selectedPageIdx.value++
      }
      dragSrcIdx.value = null
      dragOverIdx.value = null
      scheduleAutoSave(true)
    }

    function onDragEnd() {
      dragSrcIdx.value = null
      dragOverIdx.value = null
    }

    function selectPage(idx) {
      if (editingPageIdx.value !== null) return
      if (dragSrcIdx.value !== null) return
      selectedPageIdx.value = idx
      selectedPathSegments.value = []
      manualElementFilter.value = ''
      const page = props.pages.pages[idx]
      selectedPageServerId.value = page?.serverId ? `server_${page.serverId}` : 'server_0'
      if (page?.name) emit('activate-page', page.name)
    }

    function startRename(idx) {
      editingPageIdx.value = idx
      editingName.value = props.pages.pages[idx]?.name || ''
      nextTick(() => {
        const input = document.querySelector('.page-rename-input')
        if (input) { input.focus(); input.select() }
      })
    }

    function commitRename() {
      if (editingPageIdx.value === null) return
      const page = props.pages.pages[editingPageIdx.value]
      if (page && editingName.value.trim()) {
        page.name = editingName.value.trim()
        scheduleAutoSave(true)
      }
      editingPageIdx.value = null
    }

    function cancelRename() {
      const idx = editingPageIdx.value
      if (idx !== null) {
        const page = props.pages.pages[idx]
        if (page && !page.name) {
          props.pages.pages.splice(idx, 1)
          if (selectedPageIdx.value === idx) selectedPageIdx.value = null
        }
      }
      editingPageIdx.value = null
    }

    function addPage() {
      const newPage = { name: '', elements: [], serverId: 0, skipKeys: ['children'] }
      if (!props.pages.pages) props.pages.pages = []
      props.pages.pages.push(newPage)
      const idx = props.pages.pages.length - 1
      selectedPageIdx.value = idx
      nextTick(() => startRename(idx))
    }

    function deletePage(idx) {
      const pageName = props.pages.pages[idx]?.name
      props.pages.pages.splice(idx, 1)
      if (selectedPageIdx.value === idx) selectedPageIdx.value = null
      else if (selectedPageIdx.value > idx) selectedPageIdx.value--
      scheduleAutoSave(true)
      saveStatus.value = { type: 'success', message: `✓ Page "${pageName}" deleted` }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    // ── Parameter editor actions ─────────────────────────────────────────

    function setSelectedPageServer() {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      page.serverId = Number.parseInt(String(selectedPageServerId.value).replace('server_', ''), 10) || 0
      selectedPathSegments.value = []
      manualElementFilter.value = ''
      scheduleAutoSave(true)
    }

    function syncSelectedPathToFilter() { manualElementFilter.value = joinSelectedSegments() }
    function selectNextPathOption(option) { selectedPathSegments.value.push(option); syncSelectedPathToFilter() }
    function insertWildcardSegment()      { selectedPathSegments.value.push('*');    syncSelectedPathToFilter() }
    function truncatePathSelection(index) { selectedPathSegments.value = selectedPathSegments.value.slice(0, index + 1); syncSelectedPathToFilter() }
    function resetPathSelection()         { selectedPathSegments.value = []; syncSelectedPathToFilter() }
    function useSelectedPathAsFilter()    { syncSelectedPathToFilter() }
    function clearManualFilterLine()      { manualElementFilter.value = '' }

    function useSelectedPathAsPrefix() {
      const path = joinSelectedSegments()
      manualElementFilter.value = path ? `${path}/*` : ''
    }

    function normalizeFilterPath(value) {
      const trimmed = value.trim()
      if (!trimmed) return ''
      return trimmed.startsWith('/') ? trimmed : `/${trimmed}`
    }

    function addManualFilterLine() {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      const normalized = normalizeFilterPath(manualElementFilter.value)
      if (!normalized) return
      if (!Array.isArray(page.elements)) page.elements = []
      const sNum = serverIdNum(selectedPageServerId.value)
      const alreadyExists = currentPageParams.value.some(e => e.path === normalized && e.serverId === sNum)
      if (!alreadyExists) {
        page.elements.push({ path: normalized, serverId: sNum })
        scheduleAutoSave(true)
        saveStatus.value = { type: 'success', message: `✓ Filter added: ${normalized}` }
        setTimeout(() => (saveStatus.value = null), 1500)
      }
    }

    function addParameterToPage(param) {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      if (!Array.isArray(page.elements)) page.elements = []
      const sNum = serverIdNum(selectedPageServerId.value)
      const alreadyExists = currentPageParams.value.some(e => e.path === param.path && e.serverId === sNum)
      if (!alreadyExists) {
        page.elements.push({ path: param.path, serverId: sNum })
        scheduleAutoSave(true)
      }
    }

    function removeParameterFromPage(idx) {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      const toRemove = currentPageParams.value[idx]
      if (!toRemove) return
      if (Array.isArray(page.elements)) {
        const defaultSid = typeof page.serverId === 'number' ? page.serverId : 0
        const i = page.elements.findIndex(el => {
          const norm = normalizeElement(el, defaultSid)
          return norm.path === toRemove.path && norm.serverId === toRemove.serverId
        })
        if (i !== -1) { page.elements.splice(i, 1); scheduleAutoSave(true) }
        return
      }
      ;['surfaces', 'laser_surfaces', 'fixtures', 'medias'].forEach(type => {
        if (!page[type]) return
        const i = page[type].indexOf(toRemove.path)
        if (i !== -1) { page[type].splice(i, 1); scheduleAutoSave(true) }
      })
    }

    // ── Export ──────────────────────────────────────────────────────────

    function exportPages() {
      const data = { pages: props.pages.pages, subpages: props.pages.subpages || [] }
      const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = `pages-${new Date().toISOString().slice(0, 10)}.json`
      a.click()
      URL.revokeObjectURL(url)
    }

    // ── Import ──────────────────────────────────────────────────────────

    function triggerImport() {
      if (fileInputEl.value) fileInputEl.value.click()
    }

    function handleImportFile(event) {
      const file = event.target.files?.[0]
      if (!file) return
      event.target.value = ''
      const reader = new FileReader()
      reader.onload = (e) => {
        try {
          const parsed = JSON.parse(e.target.result)
          const pagesArr = Array.isArray(parsed.pages) ? parsed.pages : Array.isArray(parsed) ? parsed : []
          importDraft.value = { pages: pagesArr, subpages: parsed.subpages || [] }
          importSelections.value = new Set(pagesArr.map((_, i) => i))
          importMode.value = 'merge'
        } catch {
          saveStatus.value = { type: 'error', message: '✕ Invalid JSON file' }
          setTimeout(() => (saveStatus.value = null), 3000)
        }
      }
      reader.readAsText(file)
    }

    function toggleImportPage(idx) {
      const s = new Set(importSelections.value)
      s.has(idx) ? s.delete(idx) : s.add(idx)
      importSelections.value = s
    }

    function selectAllImport()  { importSelections.value = new Set(importDraft.value.pages.map((_, i) => i)) }
    function selectNoneImport() { importSelections.value = new Set() }

    function confirmImport() {
      if (!importDraft.value) return
      const selected = importDraft.value.pages.filter((_, i) => importSelections.value.has(i))
      if (!props.pages.pages) props.pages.pages = []
      if (importMode.value === 'replace') {
        props.pages.pages.splice(0, props.pages.pages.length, ...selected)
        selectedPageIdx.value = null
      } else {
        const existingNames = new Set(props.pages.pages.map(p => p.name))
        selected.forEach(page => {
          if (existingNames.has(page.name)) {
            const i = props.pages.pages.findIndex(p => p.name === page.name)
            if (i !== -1) props.pages.pages.splice(i, 1, { ...page })
          } else {
            props.pages.pages.push({ ...page })
          }
        })
      }
      importDraft.value = null
      scheduleAutoSave(true)
      saveStatus.value = { type: 'success', message: `✓ Imported ${selected.length} page${selected.length === 1 ? '' : 's'}` }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function cancelImport() {
      importDraft.value = null
    }

    // ── JSON editor ──────────────────────────────────────────────────────

    function validateJson() {
      try {
        const data = JSON.parse(editedJson.value)
        jsonError.value = ''
        return data
      } catch (err) {
        jsonError.value = `JSON Error: ${err.message}`
        return null
      }
    }

    function onJsonChange() { validateJson() }

    function handleJsonSave() {
      const validated = validateJson()
      if (!validated) return
      emit('save', validated)
      saveStatus.value = { type: 'success', message: '✓ Changes saved' }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function handleReset() {
      if (originalData.value) { editedJson.value = originalData.value; jsonError.value = '' }
    }

    return {
      activeTab, tabs,
      selectedPageIdx, editingPageIdx, editingName,
      selectedPageServerId, selectedPathSegments, manualElementFilter,
      editedJson, jsonError, saveStatus,
      fileInputEl, importDraft, importSelections, importMode,
      dragSrcIdx, dragOverIdx, onDragStart, onDragOver, onDragLeave, onDrop, onDragEnd,
      resolvedParameters, resolvedParameterCount,
      availableServerOptions, currentPageParams, nextPathOptions, filteredAvailableParams,
      selectPage, startRename, commitRename, cancelRename, addPage, deletePage,
      setSelectedPageServer,
      selectNextPathOption, insertWildcardSegment, truncatePathSelection,
      resetPathSelection, useSelectedPathAsFilter, useSelectedPathAsPrefix,
      addManualFilterLine, clearManualFilterLine,
      addParameterToPage, removeParameterFromPage,
      exportPages, triggerImport, handleImportFile,
      toggleImportPage, selectAllImport, selectNoneImport, confirmImport, cancelImport,
      onJsonChange, handleJsonSave, handleReset
    }
  }
}
</script>

<style scoped>
* { box-sizing: border-box; }

.page-manager {
  display: flex;
  flex-direction: column;
  height: 100%;
  color: var(--text-main);
  position: relative;
}

/* ── Tab strip ── */
.manager-tabs {
  display: flex;
  background: var(--bg-shell);
  border-bottom: 1px solid var(--border-strong);
  padding: 0 12px;
  flex-shrink: 0;
}

.tab {
  padding: 0 18px;
  height: 34px;
  border: none;
  border-bottom: 2px solid transparent;
  border-radius: 0;
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  transition: color 0.1s, border-color 0.1s;
}

.tab:hover { color: var(--text-muted); }
.tab.active { color: var(--accent); border-bottom-color: var(--accent); }

/* ── Layout ── */
.page-builder {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr);
  flex: 1;
  min-height: 0;
  overflow: hidden;
}

.page-builder.has-selection {
  grid-template-columns: 220px minmax(0, 1fr) 200px;
}

/* ── Panel sections ── */
.builder-section {
  display: flex;
  flex-direction: column;
  min-height: 0;
  border-right: 1px solid var(--border-strong);
  background: var(--bg-shell);
  overflow: hidden;
}

.builder-section.parameters {
  border-right: none;
  background: var(--bg-panel);
  overflow-y: auto;
  padding: 0 0 10px;
  display: flex;
  flex-direction: column;
}

.builder-section.parameters > *:not(.section-header) {
  padding: 10px 12px 0;
}

/* ── Section header ── */
.section-header {
  padding: 0 6px 0 10px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: var(--bg-panel-strong);
  border-bottom: 1px solid var(--border-strong);
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-dim);
  flex-shrink: 0;
}

.header-btns {
  display: flex;
  align-items: center;
  gap: 3px;
}

.header-icon-btn {
  width: 22px;
  height: 22px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: transparent;
  color: var(--text-dim);
  font-size: 14px;
  line-height: 1;
  cursor: pointer;
  flex-shrink: 0;
  padding: 0;
  transition: color 0.1s, border-color 0.1s, background 0.1s;
}

.header-icon-btn:hover {
  color: var(--accent);
  border-color: var(--accent-dim);
  background: var(--bg-active);
}

/* ── Resolved parameters column ── */
.resolved-col { background: var(--bg-panel); }

.resolved-list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.resolved-item {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 3px 6px;
  font-size: 10px;
  font-family: "SFMono-Regular", "Menlo", monospace;
  color: var(--text-muted);
  border-bottom: 1px solid var(--border-soft);
}

.resolved-item:hover { background: var(--bg-hover); color: var(--text-main); }

.resolved-path {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.resolved-item:hover .resolved-path {
  overflow: visible;
  white-space: normal;
  word-break: break-all;
}

.count-badge {
  font-size: 10px;
  font-weight: 700;
  color: var(--text-dim);
  background: var(--bg-panel-soft);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  padding: 0 5px;
  text-transform: none;
  letter-spacing: 0;
}

/* ── Pages list ── */
.pages-list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.page-item {
  display: flex;
  align-items: center;
  height: 28px;
  padding: 0 8px 0 0;
  gap: 7px;
  cursor: pointer;
  border-bottom: 1px solid var(--border-soft);
  background: transparent;
  transition: background 0.07s;
}

.page-item:hover:not(.selected):not(.dragging) { background: var(--bg-hover); }

.page-item.selected { background: rgba(18, 200, 218, 0.18); }
.page-item.selected .page-name { color: var(--accent); font-weight: 600; }

.page-item.dragging { opacity: 0.35; }

.page-item.drag-over {
  background: rgba(18, 200, 218, 0.1);
  box-shadow: inset 0 2px 0 var(--accent);
}

.page-swatch {
  width: 4px;
  height: 28px;
  flex-shrink: 0;
  background: var(--accent);
  opacity: 0.6;
  cursor: grab;
}

.page-item.dragging .page-swatch { cursor: grabbing; }

.page-item.selected .page-swatch { opacity: 1; }

.page-name {
  flex: 1;
  font-size: 12px;
  color: var(--text-muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.page-rename-input {
  flex: 1;
  height: 20px;
  padding: 0 5px;
  border: 1px solid var(--accent-dim);
  border-radius: var(--radius-xs);
  background: var(--bg-input);
  color: var(--accent);
  font-size: 12px;
  font-weight: 600;
  min-width: 0;
}

.page-rename-input:focus {
  outline: none;
  box-shadow: 0 0 0 2px rgba(18, 200, 218, 0.15);
}

.page-count { font-size: 10px; color: var(--text-dim); flex-shrink: 0; }

/* ── Sub-section headers ── */
.add-parameters,
.current-parameters,
.filter-builder {
  display: flex;
  flex-direction: column;
  gap: 7px;
}

.add-parameters,
.current-parameters {
  padding-bottom: 10px;
  border-bottom: 1px solid var(--border-soft);
}

.add-parameters:last-child,
.current-parameters:last-child { padding-bottom: 0; border-bottom: none; }

.add-parameters h4,
.current-parameters h4,
.filter-builder h4 {
  margin: 0;
  font-size: 10px;
  font-weight: 700;
  color: var(--text-dim);
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.filter-builder {
  padding: 8px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: var(--bg-shell);
}

/* Server row: label + dropdown inline */
.server-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.server-label {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.07em;
  text-transform: uppercase;
  color: var(--text-dim);
  flex-shrink: 0;
}

.server-row select {
  flex: 1;
  height: 26px;
  padding: 0 8px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  color: var(--text-main);
  background: var(--bg-input);
  font-size: 11px;
}

.server-row select:focus {
  outline: none;
  border-color: var(--accent-dim);
}

.param-selector {
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: var(--bg-shell);
  max-height: 200px;
  overflow-y: auto;
  padding: 3px;
}

.selected-path,
.next-options { display: flex; gap: 4px; flex-wrap: wrap; }

.crumb,
.chip {
  padding: 3px 8px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: var(--bg-panel-soft);
  color: var(--text-muted);
  cursor: pointer;
  font-size: 11px;
  transition: border-color 0.1s, color 0.1s;
}

.crumb:hover,
.chip:hover { border-color: var(--accent-dim); color: var(--accent); }

.chip--wildcard {
  border-color: var(--accent-dim);
  color: var(--accent);
  font-family: "SFMono-Regular", "Menlo", monospace;
  font-weight: 700;
  background: var(--bg-active);
}

.chip--wildcard:hover {
  background: rgba(18, 200, 218, 0.2);
}

.mini-btn,
.save-btn,
.reset-btn {
  border-radius: var(--radius-xs);
  cursor: pointer;
  font-size: 11px;
  font-weight: 600;
  transition: background 0.1s, opacity 0.1s;
}

.mini-btn.secondary,
.reset-btn {
  padding: 4px 10px;
  border: 1px solid var(--border-soft);
  background: var(--bg-panel-soft);
  color: var(--text-muted);
}

.mini-btn.secondary:hover,
.reset-btn:hover { background: var(--bg-hover); color: var(--text-main); }

.mini-btn,
.save-btn {
  padding: 4px 10px;
  border: 1px solid var(--accent-dim);
  background: var(--accent);
  color: #071316;
}

.mini-btn:hover,
.save-btn:not(:disabled):hover { background: var(--accent-strong); }

.save-btn,
.reset-btn { padding: 5px 14px; }
.save-btn { flex: 1; }

.filter-search { display: flex; gap: 6px; }

.filter-search input,
.filter-search select {
  flex: 1;
  padding: 4px 8px;
  height: 26px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  color: var(--text-main);
  background: var(--bg-input);
  font-size: 11px;
}

.filter-search input::placeholder { color: var(--text-dim); }

.filter-search input:focus,
.filter-search select:focus { outline: none; border-color: var(--accent-dim); }

.param-list { display: flex; flex-direction: column; }

.param-item {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 26px;
  padding: 0 8px;
  border-bottom: 1px solid var(--border-soft);
  font-size: 11px;
}

.param-srv,
.resolved-srv {
  flex-shrink: 0;
  width: 16px;
  height: 14px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 9px;
  font-weight: 700;
  border-radius: 2px;
  background: var(--bg-panel-soft);
  border: 1px solid var(--border-soft);
  color: var(--text-dim);
  letter-spacing: 0;
}

.param-path { overflow-wrap: anywhere; flex: 1; }

.param-option {
  padding: 5px 8px;
  border-bottom: 1px solid var(--border-soft);
  background: transparent;
  font-size: 11px;
}

.param-option:hover { background: var(--bg-hover); }
.param-label { display: block; cursor: pointer; color: var(--text-main); }

.delete-btn,
.remove-btn {
  width: 20px;
  height: 20px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  border: 1px solid transparent;
  border-radius: var(--radius-xs);
  background: transparent;
  color: var(--text-dim);
  font-size: 10px;
  cursor: pointer;
  flex-shrink: 0;
  transition: background 0.1s, color 0.1s, border-color 0.1s;
}

.delete-btn:hover,
.remove-btn:hover {
  color: var(--error-text);
  border-color: rgba(198, 40, 40, 0.35);
  background: rgba(198, 40, 40, 0.14);
}

.hint { margin: 0; font-size: 11px; color: var(--text-dim); line-height: 1.45; }

.hint code {
  padding: 1px 4px;
  border-radius: var(--radius-xs);
  background: rgba(255, 255, 255, 0.07);
  color: var(--accent);
  font-family: "SFMono-Regular", "Menlo", monospace;
}

.empty { text-align: center; padding: 14px 8px; font-size: 11px; color: var(--text-dim); margin: 0; }

.builder-actions,
.button-group { display: flex; gap: 6px; }

/* ── JSON editor ── */
.json-editor-section {
  display: flex;
  flex-direction: column;
  gap: 10px;
  flex: 1;
  min-height: 0;
  padding: 12px;
}

.json-editor { flex: 1; display: flex; min-height: 0; }

.json-editor textarea {
  flex: 1;
  padding: 8px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  color: var(--text-main);
  background: var(--bg-app);
  font-family: "SFMono-Regular", "Menlo", monospace;
  font-size: 11px;
  line-height: 1.5;
  resize: none;
}

.json-editor textarea:focus { outline: none; border-color: var(--accent-dim); }

/* ── Status messages ── */
.validation-message,
.save-status {
  padding: 5px 10px;
  border: 1px solid transparent;
  border-radius: var(--radius-xs);
  font-size: 11px;
  font-weight: 600;
}

.validation-message,
.save-status.success {
  color: var(--success-text);
  background: var(--success-bg);
  border-color: rgba(46, 125, 50, 0.25);
}

.validation-message.error,
.save-status.error {
  color: var(--error-text);
  background: var(--error-bg);
  border-color: rgba(198, 40, 40, 0.3);
}

.save-btn:disabled { opacity: 0.4; cursor: not-allowed; }
.save-status { animation: fadeIn 0.15s ease-out; }

@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }

/* ── Import overlay ── */
.import-overlay {
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10;
}

.import-panel {
  width: 360px;
  max-height: 80%;
  display: flex;
  flex-direction: column;
  background: var(--bg-shell);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-xs);
  overflow: hidden;
}

.import-mode-row {
  display: flex;
  gap: 16px;
  padding: 8px 12px;
  border-bottom: 1px solid var(--border-strong);
  flex-shrink: 0;
}

.mode-option {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: var(--text-muted);
  cursor: pointer;
}

.mode-option input { accent-color: var(--accent); cursor: pointer; }

.import-list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.import-row {
  display: flex;
  align-items: center;
  height: 28px;
  padding: 0 10px 0 0;
  gap: 8px;
  cursor: pointer;
  border-bottom: 1px solid var(--border-soft);
  transition: background 0.07s;
}

.import-row:hover { background: var(--bg-hover); }
.import-row.checked { background: rgba(18, 200, 218, 0.08); }

.import-swatch {
  width: 4px;
  height: 28px;
  flex-shrink: 0;
  background: var(--accent);
  opacity: 0.4;
}

.import-row.checked .import-swatch { opacity: 1; }

.import-row input[type="checkbox"] {
  accent-color: var(--accent);
  cursor: pointer;
  flex-shrink: 0;
}

.import-name { flex: 1; font-size: 12px; color: var(--text-muted); }
.import-row.checked .import-name { color: var(--accent); font-weight: 600; }
.import-count { font-size: 10px; color: var(--text-dim); flex-shrink: 0; }

.import-footer {
  display: flex;
  gap: 5px;
  padding: 8px 10px;
  border-top: 1px solid var(--border-strong);
  background: var(--bg-panel-strong);
  flex-shrink: 0;
}

.import-confirm-btn { margin-left: auto; }

.mini-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

@media (max-width: 900px) {
  .page-builder,
  .page-builder.has-selection { grid-template-columns: 1fr; }
  .builder-section { border-right: none; border-bottom: 1px solid var(--border-strong); }
}
</style>
