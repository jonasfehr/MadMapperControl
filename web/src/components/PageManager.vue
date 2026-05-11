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
              {{ page.surfaces?.length || 0 }} surfaces,
              {{ page.fixtures?.length || 0 }} fixtures,
              {{ page.medias?.length || 0 }} medias
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
          <div class="filter-search">
            <input
              v-model="parameterFilter"
              type="text"
              placeholder="Filter by path or name (supports * wildcard)..."
            />
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
              <button class="mini-btn" @click="addManualFilterLine">Add Filter Line</button>
              <button class="mini-btn secondary" @click="clearManualFilterLine">Clear</button>
            </div>
            <p class="hint">Tip: Click one of the subpath chips below to auto-compose wildcard filters.</p>
          </div>

          <div class="param-selector">
            <div
              v-for="param in filteredAvailableParams"
              :key="param.key"
              class="param-option"
            >
              <div class="param-option-header">
                <span class="param-label" @click="addParameterToPage(param)">+ {{ param.label }}</span>
              </div>
              <div class="path-chips">
                <button
                  v-for="(seg, sidx) in pathSegments(param.path)"
                  :key="`${param.key}-${sidx}`"
                  class="chip"
                  @click="buildFilterFromSubpath(param.path, sidx)"
                >
                  {{ seg }}
                </button>
              </div>
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
  emits: ['save'],
  setup(props, { emit }) {
    const activeTab = ref(0)
    const selectedPageIdx = ref(null)
    const newPageName = ref('')
    const selectedPageServerId = ref('server_0')
    const parameterFilter = ref('')
    const manualElementFilter = ref('')
    const editedJson = ref('')
    const jsonError = ref('')
    const saveStatus = ref(null)
    const originalData = ref(null)

    const tabs = ['builder', 'editor']

    function wildcardToRegExp(pattern) {
      const escaped = pattern.replace(/[.+?^${}()|[\]\\]/g, '\\$&')
      return new RegExp(`^${escaped.replace(/\*/g, '.*')}$`, 'i')
    }

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

    // Computed: Verfügbare Parameter (nicht bereits in der page)
    const filteredAvailableParams = computed(() => {
      const current = new Set(currentPageParams.value)
      const query = parameterFilter.value.trim()
      const hasWildcard = query.includes('*')
      const wildcardRegex = hasWildcard ? wildcardToRegExp(query) : null

      return props.allParameters
        .filter(p => p && p.serverId === selectedPageServerId.value)
        .filter(p => !current.has(p.path))
        .filter(p => {
          if (!query) return true
          const haystack = `${p.label} ${p.path}`
          if (wildcardRegex) return wildcardRegex.test(p.path) || wildcardRegex.test(haystack)
          return haystack.toLowerCase().includes(query.toLowerCase())
        })
        .sort((a, b) => a.path.localeCompare(b.path))
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
      parameterFilter.value = ''
      const page = props.pages.pages[idx]
      selectedPageServerId.value = page?.serverId ? `server_${page.serverId}` : 'server_0'
    }

    function setSelectedPageServer() {
      if (selectedPageIdx.value === null) return
      const page = props.pages.pages[selectedPageIdx.value]
      if (!page) return
      const parsed = String(selectedPageServerId.value).replace('server_', '')
      page.serverId = Number.parseInt(parsed, 10) || 0
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

    function pathSegments(path) {
      if (!path) return []
      return path.split('/').filter(Boolean)
    }

    function buildFilterFromSubpath(path, segmentIndex) {
      const segments = pathSegments(path)
      if (!segments.length) return
      const prefix = segments.slice(0, segmentIndex + 1).join('/')
      manualElementFilter.value = `/${prefix}/*`
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

      saveStatus.value = {
        type: 'success',
        message: `✓ Page "${newPage.name}" created`
      }
      setTimeout(() => (saveStatus.value = null), 2000)
    }

    function deletePage(idx) {
      const pageName = props.pages.pages[idx]?.name
      props.pages.pages.splice(idx, 1)
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
            }
          }
        })
      }
    }

    function savePageChanges() {
      // Convert internal format to backend format before saving
      const convertedPages = props.pages.pages.map(page => {
        // If page already has 'elements', keep it (it's in old format)
        if (Array.isArray(page.elements)) {
          return {
            ...page,
            serverId: typeof page.serverId === 'number' ? page.serverId : 0,
            skipKeys: page.skipKeys || ['children']
          }
        }
        
        // Otherwise, convert from new format to old format
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

      const convertedData = {
        pages: convertedPages,
        subpages: props.pages.subpages || []
      }

      emit('save', convertedData)

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
      parameterFilter,
      manualElementFilter,
      editedJson,
      jsonError,
      saveStatus,
      availableServerOptions,
      currentPageParams,
      filteredAvailableParams,
      selectPage,
      setSelectedPageServer,
      addManualFilterLine,
      clearManualFilterLine,
      pathSegments,
      buildFilterFromSubpath,
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
  padding: 1.5rem;
  gap: 1rem;
}

.manager-tabs {
  display: flex;
  gap: 0.5rem;
  border-bottom: 2px solid #e0e0e0;
}

.tab {
  padding: 0.75rem 1.5rem;
  background: transparent;
  border: none;
  border-bottom: 3px solid transparent;
  cursor: pointer;
  font-weight: 500;
  color: #666;
  transition: all 0.2s;
}

.tab:hover {
  color: #333;
}

.tab.active {
  color: #667eea;
  border-bottom-color: #667eea;
}

/* Page Builder Styles */
.page-builder {
  display: flex;
  gap: 2rem;
  flex: 1;
  overflow: hidden;
}

.builder-section {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  background: white;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  padding: 1rem;
  overflow: hidden;
}

.builder-section:first-child {
  width: 300px;
  flex-shrink: 0;
}

.builder-section.parameters {
  flex: 1;
  overflow: auto;
}

.builder-section h3 {
  margin: 0;
  font-size: 1rem;
  color: #333;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid #e0e0e0;
}

.builder-section > h4 {
  margin: 1rem 0 0.5rem 0;
  font-size: 0.9rem;
  color: #666;
}

.pages-list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.page-item {
  padding: 0.75rem;
  background: #f5f5f5;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}

.page-item:hover {
  background: #efefef;
}

.page-item.selected {
  background: #e3f2fd;
  border-color: #667eea;
  box-shadow: 0 0 0 2px rgba(102, 126, 234, 0.1);
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.25rem;
}

.page-name {
  font-weight: 500;
  font-size: 0.9rem;
}

.delete-btn {
  background: transparent;
  border: none;
  color: #999;
  cursor: pointer;
  font-size: 1rem;
  padding: 0;
  width: 24px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 2px;
  transition: all 0.2s;
}

.delete-btn:hover {
  background: #ffebee;
  color: #c62828;
}

.page-info {
  font-size: 0.75rem;
  color: #999;
}

.new-page-form {
  display: flex;
  gap: 0.5rem;
  padding-top: 1rem;
  border-top: 1px solid #e0e0e0;
}

.new-page-form input {
  flex: 1;
  padding: 0.5rem 0.75rem;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-size: 0.85rem;
}

.add-btn {
  padding: 0.5rem 1rem;
  background: #667eea;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-weight: 500;
  white-space: nowrap;
  transition: all 0.2s;
}

.add-btn:hover {
  background: #764ba2;
}

/* Parameters Section */
.add-parameters,
.current-parameters {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  margin-bottom: 1rem;
  padding-bottom: 1rem;
  border-bottom: 1px solid #e0e0e0;
}

.add-parameters:last-child,
.current-parameters:last-child {
  border-bottom: none;
  margin-bottom: 0;
  padding-bottom: 0;
}

.add-parameters h4,
.current-parameters h4 {
  margin: 0;
  font-size: 0.9rem;
  color: #666;
}

.filter-builder {
  border: 1px solid #e6e6e6;
  border-radius: 4px;
  padding: 0.65rem;
  background: #fcfcfc;
}

.filter-builder h4 {
  margin: 0 0 0.5rem 0;
  font-size: 0.85rem;
  color: #666;
}

.builder-actions {
  display: flex;
  gap: 0.5rem;
  margin-top: 0.5rem;
}

.mini-btn {
  border: none;
  background: #667eea;
  color: white;
  border-radius: 4px;
  padding: 0.45rem 0.75rem;
  cursor: pointer;
  font-size: 0.8rem;
}

.mini-btn.secondary {
  background: #e0e0e0;
  color: #333;
}

.hint {
  margin: 0.5rem 0 0 0;
  color: #777;
  font-size: 0.78rem;
}

.filter-search {
  display: flex;
}

.filter-search input {
  flex: 1;
  padding: 0.5rem 0.75rem;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-size: 0.85rem;
}

.filter-search select {
  flex: 1;
  padding: 0.5rem 0.75rem;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-size: 0.85rem;
  background: white;
}

.param-selector {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  max-height: 200px;
  overflow-y: auto;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  background: #fafafa;
}

.param-option {
  padding: 0.5rem 0.75rem;
  font-size: 0.85rem;
  transition: all 0.2s;
  border-bottom: 1px solid #f0f0f0;
}

.param-option:last-child {
  border-bottom: none;
}

.param-option:hover {
  background: #e8f4f8;
  color: #667eea;
}

.param-option-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.param-label {
  cursor: pointer;
  font-weight: 500;
}

.path-chips {
  display: flex;
  gap: 0.35rem;
  flex-wrap: wrap;
  margin-top: 0.4rem;
}

.chip {
  border: 1px solid #d9e2ff;
  background: #eef2ff;
  color: #445;
  border-radius: 999px;
  padding: 0.2rem 0.5rem;
  font-size: 0.72rem;
  cursor: pointer;
}

.chip:hover {
  background: #dde5ff;
}

.param-list {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  max-height: 250px;
  overflow-y: auto;
}

.param-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.5rem 0.75rem;
  background: #f5f5f5;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-size: 0.85rem;
}

.remove-btn {
  background: transparent;
  border: none;
  color: #999;
  cursor: pointer;
  font-size: 0.9rem;
  padding: 0;
  width: 20px;
  height: 20px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 2px;
  transition: all 0.2s;
}

.remove-btn:hover {
  background: #ffebee;
  color: #c62828;
}

.empty {
  color: #999;
  font-size: 0.85rem;
  text-align: center;
  padding: 1rem 0.5rem;
  margin: 0;
}

.action-buttons {
  display: flex;
  gap: 0.75rem;
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid #e0e0e0;
}

.save-btn,
.cancel-btn {
  padding: 0.75rem 1.5rem;
  border: none;
  border-radius: 4px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  font-size: 0.9rem;
}

.save-btn {
  background: #667eea;
  color: white;
  flex: 1;
}

.save-btn:hover {
  background: #764ba2;
}

.cancel-btn {
  background: #f5f5f5;
  color: #333;
  border: 1px solid #e0e0e0;
}

.cancel-btn:hover {
  background: #e0e0e0;
}

/* JSON Editor Section */
.json-editor-section {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  flex: 1;
  overflow: hidden;
}

.json-editor {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.json-editor textarea {
  flex: 1;
  padding: 1rem;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', monospace;
  font-size: 0.85rem;
  resize: none;
  background: #fafafa;
}

.json-editor textarea:focus {
  outline: none;
  border-color: #667eea;
  background: white;
}

.validation-message {
  padding: 0.5rem 1rem;
  border-radius: 4px;
  font-size: 0.85rem;
  color: #2e7d32;
  background: #e8f5e9;
}

.validation-message.error {
  color: #c62828;
  background: #ffebee;
}

.button-group {
  display: flex;
  gap: 0.75rem;
}

.button-group .save-btn,
.button-group .reset-btn {
  padding: 0.75rem 1.5rem;
  border: none;
  border-radius: 4px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.button-group .save-btn {
  background: #667eea;
  color: white;
  flex: 1;
}

.button-group .save-btn:hover:not(:disabled) {
  background: #764ba2;
}

.button-group .save-btn:disabled {
  background: #ccc;
  cursor: not-allowed;
}

.button-group .reset-btn {
  background: #f5f5f5;
  color: #333;
  border: 1px solid #e0e0e0;
}

.button-group .reset-btn:hover {
  background: #e0e0e0;
}

.save-status {
  padding: 0.75rem 1rem;
  border-radius: 4px;
  font-weight: 500;
  animation: slideIn 0.3s ease-out;
}

.save-status.success {
  background: #e8f5e9;
  color: #2e7d32;
}

.save-status.error {
  background: #ffebee;
  color: #c62828;
}

@keyframes slideIn {
  from {
    transform: translateY(-10px);
    opacity: 0;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}
</style>
