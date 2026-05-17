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
        {{ tab === 'editor' ? '📝 Page Editor' : '⚙️ Page Builder' }}
      </button>
    </div>

    <!-- Tab 0: Page Builder (neue UI) -->
    <div v-if="activeTab === 0" class="page-builder">
      <div class="builder-section">
        <h3>📋 Pages</h3>
        <div class="pages-list">
          <div
            v-for="(page, idx) in pages.pages"
            :key="idx"
            class="page-item"
            :class="{ selected: selectedPageIdx === idx }"
            @click="selectPage(idx)"
          >
            <div class="page-header">
              <span class="page-name">{{ page.name || 'Unnamed' }}</span>
              <button class="delete-btn" @click.stop="deletePage(idx)">✕</button>
            </div>
            <div class="page-info">
              {{ pageItemCount(page) }} items · {{ resolvedParameterCount(page) }} parameters
            </div>
          </div>
        </div>

        <div class="new-page-form">
          <input
            v-model="newPageName"
            type="text"
            placeholder="New page name..."
            @keyup.enter="createNewPage"
          />
          <button @click="createNewPage" class="add-btn">+ New Page</button>
        </div>
      </div>

      <div v-if="selectedPageIdx !== null" class="builder-section parameters">
        <h3>🎛 Parameters for "{{ pages.pages[selectedPageIdx]?.name }}"</h3>

        <!-- Available Parameters to add -->
        <div class="add-parameters">
          <h4>Available Parameters</h4>
          <div class="filter-search">
            <select v-model="selectedPageServerId" @change="setSelectedPageServer">
              <option
                v-for="server in availableServerOptions"
                :key="server.id"
                :value="server.id"
              >
                {{ server.name }}
              </option>
            </select>
          </div>

          <div class="path-builder">
            <h4>Path Selection</h4>
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
              <button
                v-for="option in nextPathOptions"
                :key="option"
                class="chip"
                @click="selectNextPathOption(option)"
              >
                {{ option }}
              </button>
            </div>
            <button
              v-if="selectedPathSegments.length"
              class="wildcard-btn"
              @click="insertWildcardSegment"
              title="Insert wildcard segment *"
            >
              Insert <code>*</code> Segment
            </button>
            <p v-if="!nextPathOptions.length" class="hint">No further path parts. Choose a parameter below or go one step back.</p>
          </div>

          <div class="filter-builder">
            <h4>Filter Line Builder</h4>
            <div class="filter-search">
              <input
                v-model="manualElementFilter"
                type="text"
                placeholder="e.g. /project_waves/Custom/*"
              />
            </div>
            <div class="builder-actions">
              <button class="mini-btn secondary" @click="useSelectedPathAsFilter">Use Selected Path</button>
              <button class="mini-btn secondary" @click="useSelectedPathAsPrefix">Add Trailing *</button>
              <button class="mini-btn" @click="addManualFilterLine">Add Filter Line</button>
              <button class="mini-btn secondary" @click="clearManualFilterLine">Clear</button>
            </div>
            <p class="hint">Tip: You can build filters like <code>/surfaces/*/opacity</code> by inserting <code>*</code> between path segments.</p>
          </div>

          <div class="param-selector">
            <div
              v-for="param in filteredAvailableParams"
              :key="param.key"
              class="param-option"
            >
              <span class="param-label" @click="addParameterToPage(param)">+ {{ param.path }}</span>
            </div>
            <p v-if="!filteredAvailableParams.length" class="empty">
              No parameters available
            </p>
          </div>
        </div>

        <!-- Current Parameters -->
        <div class="current-parameters">
          <h4>Current Parameters</h4>
          <div class="param-list">
            <div
              v-for="(item, idx) in currentPageParams"
              :key="idx"
              class="param-item"
            >
              <span>{{ item }}</span>
              <button
                class="remove-btn"
                @click="removeParameterFromPage(idx)"
              >
                ✕
              </button>
            </div>
            <p v-if="!currentPageParams.length" class="empty">
              No parameters yet
            </p>
          </div>
        </div>

        <div class="action-buttons">
          <button @click="savePageChanges" class="save-btn">💾 Save Changes</button>
          <button @click="selectedPageIdx = null" class="cancel-btn">Cancel</button>
        </div>
      </div>
    </div>

    <!-- Tab 1: JSON Editor (alte UI) -->
    <div v-if="activeTab === 1" class="json-editor-section">
      <div class="json-editor">
        <textarea
          v-model="editedJson"
          placeholder="Edit pages JSON here..."
          @input="onJsonChange"
        ></textarea>
      </div>
      <div class="validation-message" :class="{ error: jsonError }">
        {{ jsonError || 'JSON is valid' }}
      </div>

      <div class="button-group">
        <button class="save-btn" @click="handleJsonSave" :disabled="!!jsonError">
          💾 Save Changes
        </button>
        <button class="reset-btn" @click="handleReset">↶ Undo</button>
      </div>
    </div>

    <div v-if="saveStatus" class="save-status" :class="saveStatus.type">
      {{ saveStatus.message }}
    </div>
  </div>
</template>

<script>
import { ref, computed, watch } from 'vue'

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
    const newPageName = ref('')
    const selectedPageServerId = ref('server_0')
    const selectedPathSegments = ref([])
    const manualElementFilter = ref('')
    const editedJson = ref('')
    const jsonError = ref('')
    const saveStatus = ref(null)
    const originalData = ref(null)
    let autoSaveTimer = null

    const tabs = ['builder', 'editor']

    const availableServerOptions = computed(() => {
      const map = new Map()
      props.allParameters.forEach(p => {
        if (!p || !p.serverId) return
        if (!map.has(p.serverId)) {
          map.set(p.serverId, {
            id: p.serverId,
            name: p.serverName || p.serverId
          })
        }
      })
      if (!map.size) {
        map.set('server_0', { id: 'server_0', name: 'MadMapper' })
      }
      return [...map.values()]
    })

    // Computed: Parameters bereits in der selected page
    const currentPageParams = computed(() => {
      if (selectedPageIdx.value === null) return []
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return []
      if (Array.isArray(page.elements)) return [...page.elements]
      return [
        ...(page.surfaces || []),
        ...(page.fixtures || []),
        ...(page.medias || [])
      ]
    })

    const availableParamsForServer = computed(() => {
      const current = new Set(currentPageParams.value)
      return props.allParameters
        .filter(p => p && p.serverId === selectedPageServerId.value)
        .filter(p => !current.has(p.path))
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

    function pageItemCount(page) {
      if (!page) return 0
      if (Array.isArray(page.elements)) return page.elements.length
      return (page.surfaces?.length || 0) + (page.fixtures?.length || 0) + (page.medias?.length || 0)
    }

    function pagePatterns(page) {
      if (!page) return []
      if (Array.isArray(page.elements)) return page.elements

      const patterns = []
      if (Array.isArray(page.surfaces)) {
        page.surfaces.forEach(surface => patterns.push(`/surfaces/${surface}/*`))
      }
      if (Array.isArray(page.fixtures)) {
        page.fixtures.forEach(fixture => patterns.push(`/fixtures/${fixture}/*`))
      }
      if (Array.isArray(page.medias)) {
        page.medias.forEach(media => patterns.push(`/media/${media}/*`))
      }
      return patterns
    }

    function resolvedParameterCount(page) {
      if (!page || !props.allParameters.length) return 0

      const pageServerId = typeof page.serverId === 'number' ? `server_${page.serverId}` : 'server_0'
      const patterns = pagePatterns(page)
      if (!patterns.length) return 0

      const resolved = new Set()
      props.allParameters.forEach(param => {
        if (!param || param.serverId !== pageServerId) return
        if (patterns.some(pattern => matchesWildcardFilter(param.path, pattern))) {
          resolved.add(param.path)
        }
      })
      return resolved.size
    }

    const filteredAvailableParams = computed(() => {
      let result = availableParamsForServer.value

      // Filter by path selection
      if (selectedPathSegments.value.length) {
        result = result.filter(param => startsWithSegments(param.path, selectedPathSegments.value))
      }

      // Filter by manual filter input
      const manualFilter = manualElementFilter.value.trim()
      if (manualFilter) {
        result = result.filter(param => matchesWildcardFilter(param.path, manualFilter))
      }

      return result
    })

    // Watch: Wenn props.pages ändert, JSON aktualisieren
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

    function selectPage(idx) {
      selectedPageIdx.value = idx
      selectedPathSegments.value = []
      manualElementFilter.value = ''
      const page = props.pages.pages[idx]
      selectedPageServerId.value = page?.serverId ? `server_${page.serverId}` : 'server_0'
      if (page?.name) {
        emit('activate-page', page.name)
      }
    }

    function setSelectedPageServer() {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      const parsed = String(selectedPageServerId.value).replace('server_', '')
      page.serverId = Number.parseInt(parsed, 10) || 0
      selectedPathSegments.value = []
      manualElementFilter.value = ''
      scheduleAutoSave()
    }

    function buildSavePayload(liveReload = true) {
      const convertedPages = props.pages.pages.map(page => {
        if (Array.isArray(page.elements)) {
          return {
            ...page,
            serverId: typeof page.serverId === 'number' ? page.serverId : 0,
            skipKeys: page.skipKeys || ['children']
          }
        }

        const elements = []
        if (page.surfaces) {
          page.surfaces.forEach(surface => {
            elements.push(`/surfaces/${surface}/*`)
          })
        }
        if (page.fixtures) {
          page.fixtures.forEach(fixture => {
            elements.push(`/fixtures/${fixture}/*`)
          })
        }
        if (page.medias) {
          page.medias.forEach(media => {
            elements.push(`/media/${media}/*`)
          })
        }

        return {
          name: page.name,
          elements: elements.length > 0 ? elements : ["/surfaces/*/opacity"],
          serverId: page.serverId || 0,
          skipKeys: page.skipKeys || ["children"]
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

    function scheduleAutoSave(liveReload = false) {
      if (selectedPageIdx.value === null) return
      clearTimeout(autoSaveTimer)
      autoSaveTimer = setTimeout(() => {
        emit('save', buildSavePayload(liveReload))
      }, 300)
    }

    function syncSelectedPathToFilter() {
      manualElementFilter.value = joinSelectedSegments()
    }

    function selectNextPathOption(option) {
      selectedPathSegments.value.push(option)
      syncSelectedPathToFilter()
    }

    function insertWildcardSegment() {
      selectedPathSegments.value.push('*')
      syncSelectedPathToFilter()
    }

    function truncatePathSelection(index) {
      selectedPathSegments.value = selectedPathSegments.value.slice(0, index + 1)
      syncSelectedPathToFilter()
    }

    function resetPathSelection() {
      selectedPathSegments.value = []
      syncSelectedPathToFilter()
    }

    function useSelectedPathAsFilter() {
      syncSelectedPathToFilter()
    }

    function useSelectedPathAsPrefix() {
      const path = joinSelectedSegments()
      if (!path) {
        manualElementFilter.value = ''
        return
      }
      manualElementFilter.value = `${path}/*`
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
      if (!page.elements.includes(normalized)) {
        page.elements.push(normalized)
        scheduleAutoSave(true)
        saveStatus.value = {
          type: 'success',
          message: `✓ Filter line added: ${normalized}`
        }
        setTimeout(() => (saveStatus.value = null), 1500)
      }
    }

    function clearManualFilterLine() {
      manualElementFilter.value = ''
    }

    function createNewPage() {
      if (!newPageName.value.trim()) {
        saveStatus.value = {
          type: 'error',
          message: '⚠️ Page name cannot be empty'
        }
        setTimeout(() => (saveStatus.value = null), 3000)
        return
      }

      const newPage = {
        name: newPageName.value,
        elements: [],
        serverId: Number.parseInt(String(selectedPageServerId.value).replace('server_', ''), 10) || 0,
        skipKeys: ['children']
      }

      // Add to our local copy
      if (!props.pages.pages) {
        props.pages.pages = []
      }
      props.pages.pages.push(newPage)
      newPageName.value = ''
      scheduleAutoSave()

      saveStatus.value = {
        type: 'success',
        message: `✓ Page "${newPage.name}" created`
      }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function deletePage(idx) {
      const pageName = props.pages.pages[idx]?.name
      props.pages.pages.splice(idx, 1)
      scheduleAutoSave()
      if (selectedPageIdx.value === idx) {
        selectedPageIdx.value = null
      }

      saveStatus.value = {
        type: 'success',
        message: `✓ Page "${pageName}" deleted`
      }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function addParameterToPage(param) {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return

      if (!Array.isArray(page.elements)) page.elements = []
      if (typeof page.serverId !== 'number') {
        page.serverId = Number.parseInt(String(selectedPageServerId.value).replace('server_', ''), 10) || 0
      }

      if (!page.elements.includes(param.path)) {
        page.elements.push(param.path)
        scheduleAutoSave()
      }
    }

    function removeParameterFromPage(idx) {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return

      const params = currentPageParams.value
      const removedParam = params[idx]
      if (removedParam) {
        if (Array.isArray(page.elements)) {
          page.elements = page.elements.filter(p => p !== removedParam)
          return
        }
        ;['surfaces', 'fixtures', 'medias'].forEach(type => {
          if (page[type]) {
            const paramIdx = page[type].indexOf(removedParam)
            if (paramIdx !== -1) {
              page[type].splice(paramIdx, 1)
              scheduleAutoSave()
            }
          }
        })
      }
    }

    function savePageChanges() {
      clearTimeout(autoSaveTimer)
      emit('save', buildSavePayload(true))

      saveStatus.value = {
        type: 'success',
        message: '✓ Pages saved successfully'
      }
      setTimeout(() => (saveStatus.value = null), 2000)
      selectedPageIdx.value = null
    }

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

    function onJsonChange() {
      validateJson()
    }

    function handleJsonSave() {
      const validated = validateJson()
      if (!validated) return

      emit('save', validated)

      saveStatus.value = {
        type: 'success',
        message: '✓ Changes saved'
      }

      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function handleReset() {
      if (originalData.value) {
        editedJson.value = originalData.value
        jsonError.value = ''
      }
    }

    return {
      activeTab,
      tabs,
      selectedPageIdx,
      newPageName,
      selectedPageServerId,
      selectedPathSegments,
      manualElementFilter,
      editedJson,
      jsonError,
      saveStatus,
      pageItemCount,
      resolvedParameterCount,
      availableServerOptions,
      currentPageParams,
      nextPathOptions,
      filteredAvailableParams,
      selectPage,
      setSelectedPageServer,
      selectNextPathOption,
      insertWildcardSegment,
      truncatePathSelection,
      resetPathSelection,
      useSelectedPathAsFilter,
      useSelectedPathAsPrefix,
      addManualFilterLine,
      clearManualFilterLine,
      createNewPage,
      deletePage,
      addParameterToPage,
      removeParameterFromPage,
      savePageChanges,
      onJsonChange,
      handleJsonSave,
      handleReset
    }
  }
}
</script>

<style scoped>
* {
  box-sizing: border-box;
}

.page-manager {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 1rem;
  gap: 1rem;
  color: var(--text-main);
}

.manager-tabs {
  display: inline-flex;
  gap: 0.45rem;
  align-self: flex-start;
  padding: 0.35rem;
  border: 1px solid var(--border-strong);
  border-radius: 14px;
  background: linear-gradient(180deg, rgba(56, 56, 56, 0.9), rgba(37, 37, 37, 0.96));
}

.tab {
  min-width: 160px;
  padding: 0.7rem 1rem;
  border: 1px solid transparent;
  border-radius: 10px;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
  font-weight: 700;
  letter-spacing: 0.01em;
  transition: background 0.18s ease, color 0.18s ease, border-color 0.18s ease;
}

.tab:hover {
  color: var(--text-main);
  background: rgba(255, 255, 255, 0.04);
}

.tab.active {
  color: #061518;
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  border-color: rgba(24, 200, 218, 0.35);
}

.page-builder {
  display: grid;
  grid-template-columns: minmax(280px, 320px) minmax(0, 1fr);
  gap: 1rem;
  flex: 1;
  min-height: 0;
}

.builder-section {
  display: flex;
  flex-direction: column;
  gap: 0.9rem;
  min-height: 0;
  padding: 1rem;
  border: 1px solid var(--border-strong);
  border-radius: 18px;
  background: linear-gradient(180deg, rgba(42, 42, 42, 0.98), rgba(30, 30, 30, 0.98));
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.04);
}

.builder-section.parameters {
  overflow: auto;
}

.builder-section h3 {
  margin: 0;
  padding-bottom: 0.75rem;
  border-bottom: 1px solid var(--border-soft);
  font-size: 0.98rem;
  font-weight: 700;
  letter-spacing: 0.01em;
  color: var(--text-main);
}

.pages-list,
.param-list {
  display: flex;
  flex-direction: column;
  gap: 0.45rem;
  min-height: 0;
  overflow-y: auto;
}

.page-item,
.param-item,
.param-option {
  border: 1px solid var(--border-soft);
  border-radius: 10px;
  background: linear-gradient(180deg, rgba(55, 55, 55, 0.96), rgba(42, 42, 42, 0.96));
}

.page-item {
  padding: 0.8rem 0.85rem;
  cursor: pointer;
  transition: border-color 0.18s ease, background 0.18s ease, transform 0.18s ease;
}

.page-item:hover,
.param-option:hover {
  background: linear-gradient(180deg, rgba(63, 63, 63, 0.96), rgba(46, 46, 46, 0.96));
  border-color: #5a5a5a;
}

.page-item.selected {
  background: linear-gradient(180deg, rgba(14, 130, 145, 0.95), rgba(10, 99, 112, 0.95));
  border-color: rgba(24, 200, 218, 0.5);
  transform: translateY(-1px);
  box-shadow: inset 0 0 0 1px rgba(24, 200, 218, 0.15);
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.3rem;
}

.page-name {
  font-weight: 700;
  font-size: 0.92rem;
}

.page-info,
.hint,
.empty {
  color: var(--text-dim);
  font-size: 0.78rem;
}

.empty {
  margin: 0;
  text-align: center;
  padding: 1rem 0.6rem;
}

.delete-btn,
.remove-btn {
  width: 24px;
  height: 24px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  border: 1px solid transparent;
  border-radius: 8px;
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  transition: background 0.18s ease, color 0.18s ease, border-color 0.18s ease;
}

.delete-btn:hover,
.remove-btn:hover {
  color: var(--error-text);
  border-color: rgba(198, 40, 40, 0.35);
  background: rgba(198, 40, 40, 0.14);
}

.new-page-form,
.filter-search,
.builder-actions,
.action-buttons,
.button-group {
  display: flex;
  gap: 0.6rem;
}

.new-page-form {
  padding-top: 0.9rem;
  border-top: 1px solid var(--border-soft);
}

.add-parameters,
.current-parameters,
.filter-builder,
.path-builder {
  display: flex;
  flex-direction: column;
  gap: 0.7rem;
}

.add-parameters,
.current-parameters {
  margin-bottom: 0.4rem;
  padding-bottom: 0.9rem;
  border-bottom: 1px solid var(--border-soft);
}

.add-parameters:last-child,
.current-parameters:last-child {
  margin-bottom: 0;
  padding-bottom: 0;
  border-bottom: none;
}

.add-parameters h4,
.current-parameters h4,
.path-builder h4,
.filter-builder h4 {
  margin: 0;
  font-size: 0.84rem;
  font-weight: 700;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.filter-builder,
.path-builder,
.param-selector,
.json-editor textarea {
  border: 1px solid var(--border-soft);
  border-radius: 14px;
  background: linear-gradient(180deg, rgba(35, 35, 35, 0.95), rgba(28, 28, 28, 0.98));
}

.filter-builder,
.path-builder {
  padding: 0.8rem;
}

.selected-path,
.next-options {
  display: flex;
  gap: 0.45rem;
  flex-wrap: wrap;
}

.selected-path {
  margin-bottom: 0.2rem;
}

.crumb,
.chip,
.wildcard-btn,
.mini-btn,
.add-btn,
.save-btn,
.cancel-btn,
.reset-btn {
  border-radius: 10px;
  transition: background 0.18s ease, border-color 0.18s ease, color 0.18s ease, transform 0.18s ease;
}

.crumb,
.chip {
  padding: 0.34rem 0.65rem;
  border: 1px solid var(--border-soft);
  background: linear-gradient(180deg, rgba(65, 65, 65, 0.95), rgba(50, 50, 50, 0.95));
  color: var(--text-main);
  cursor: pointer;
  font-size: 0.76rem;
}

.crumb:hover,
.chip:hover,
.wildcard-btn:hover,
.mini-btn:hover,
.add-btn:hover,
.save-btn:hover,
.cancel-btn:hover,
.reset-btn:hover {
  transform: translateY(-1px);
}

.crumb:hover,
.chip:hover {
  border-color: rgba(24, 200, 218, 0.35);
  background: linear-gradient(180deg, rgba(71, 71, 71, 0.96), rgba(56, 56, 56, 0.96));
}

.wildcard-btn,
.mini-btn.secondary,
.cancel-btn,
.reset-btn {
  border: 1px solid var(--border-soft);
  background: linear-gradient(180deg, rgba(73, 73, 73, 0.94), rgba(56, 56, 56, 0.94));
  color: var(--text-main);
}

.mini-btn,
.add-btn,
.save-btn {
  border: 1px solid rgba(24, 200, 218, 0.35);
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  color: #061518;
  font-weight: 800;
}

.mini-btn,
.wildcard-btn {
  padding: 0.55rem 0.85rem;
  font-size: 0.8rem;
  cursor: pointer;
}

.add-btn,
.save-btn,
.cancel-btn,
.reset-btn {
  padding: 0.75rem 1rem;
  cursor: pointer;
  font-size: 0.88rem;
  font-weight: 700;
}

.save-btn {
  flex: 1;
}

.hint {
  margin: 0;
  line-height: 1.45;
}

.hint code,
.wildcard-btn code {
  padding: 0.08rem 0.32rem;
  border-radius: 6px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--accent);
}

.filter-search input,
.filter-search select,
.json-editor textarea {
  width: 100%;
  padding: 0.72rem 0.85rem;
  border: 1px solid var(--border-soft);
  color: var(--text-main);
  background: var(--bg-input);
}

.filter-search input::placeholder,
.json-editor textarea::placeholder {
  color: var(--text-dim);
}

.filter-search input:focus,
.filter-search select:focus,
.json-editor textarea:focus {
  outline: none;
  border-color: rgba(24, 200, 218, 0.55);
  box-shadow: 0 0 0 3px rgba(24, 200, 218, 0.12);
}

.param-selector {
  max-height: 220px;
  overflow-y: auto;
  padding: 0.35rem;
}

.param-option {
  padding: 0.55rem 0.7rem;
}

.param-option + .param-option {
  margin-top: 0.35rem;
}

.param-label {
  display: block;
  cursor: pointer;
  color: var(--text-main);
  font-weight: 600;
}

.param-item {
  padding: 0.62rem 0.75rem;
  gap: 0.75rem;
}

.param-item span {
  overflow-wrap: anywhere;
}

.action-buttons {
  margin-top: auto;
  padding-top: 1rem;
  border-top: 1px solid var(--border-soft);
}

.json-editor-section {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  flex: 1;
  min-height: 0;
}

.json-editor {
  flex: 1;
  display: flex;
  min-height: 0;
}

.json-editor textarea {
  resize: none;
  font-family: "SFMono-Regular", "Menlo", monospace;
  font-size: 0.84rem;
}

.validation-message,
.save-status {
  padding: 0.75rem 0.9rem;
  border: 1px solid transparent;
  border-radius: 12px;
  font-size: 0.84rem;
  font-weight: 700;
}

.validation-message,
.save-status.success {
  color: var(--success-text);
  background: var(--success-bg);
  border-color: rgba(46, 125, 50, 0.28);
}

.validation-message.error,
.save-status.error {
  color: var(--error-text);
  background: var(--error-bg);
  border-color: rgba(198, 40, 40, 0.34);
}

.button-group .save-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
  transform: none;
}

.save-status {
  animation: slideIn 0.22s ease-out;
}

@keyframes slideIn {
  from {
    transform: translateY(-8px);
    opacity: 0;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}

@media (max-width: 1000px) {
  .page-builder {
    grid-template-columns: 1fr;
  }

  .builder-section {
    min-height: auto;
  }

  .builder-actions,
  .action-buttons,
  .new-page-form,
  .button-group {
    flex-wrap: wrap;
  }
}
</style>
