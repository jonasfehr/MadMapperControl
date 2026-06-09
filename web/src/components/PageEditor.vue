<template>
  <div class="page-editor">
    <div class="editor-tabs">
      <button
        v-for="(tab, idx) in tabs"
        :key="idx"
        class="tab"
        :class="{ active: activeTab === idx }"
        @click="activeTab = idx"
      >
        {{ tab }}
      </button>
    </div>

    <div class="editor-content">
      <div v-if="activeTab === 0" class="pages-section">
        <h3>Pages</h3>
        <div class="json-editor">
          <textarea
            v-model="editedPagesJson"
            placeholder="Edit pages JSON here..."
            @input="onJsonChange"
          ></textarea>
        </div>
        <div class="validation-message" :class="{ error: jsonError }">
          {{ jsonError || 'JSON is valid' }}
        </div>
      </div>

      <div v-else class="subpages-section">
        <h3>Subpages</h3>
        <div class="json-editor">
          <textarea
            v-model="editedSubpagesJson"
            placeholder="Edit subpages JSON here..."
            @input="onJsonChange"
          ></textarea>
        </div>
        <div class="validation-message" :class="{ error: jsonError }">
          {{ jsonError || 'JSON is valid' }}
        </div>
      </div>
    </div>

    <div class="button-group">
      <button class="save-btn" @click="handleSave" :disabled="!!jsonError">
        💾 Save Changes
      </button>
      <button class="reset-btn" @click="handleReset">↶ Undo</button>
    </div>

    <div v-if="saveStatus" class="save-status" :class="saveStatus.type">
      {{ saveStatus.message }}
    </div>
  </div>
</template>

<script>
import { ref, computed, watch } from 'vue'

export default {
  name: 'PageEditor',
  props: {
    pages: {
      type: Object,
      default: () => ({ pages: [], subpages: [] })
    }
  },
  emits: ['save'],
  setup(props, { emit }) {
    const activeTab = ref(0)
    const editedPagesJson = ref('')
    const editedSubpagesJson = ref('')
    const jsonError = ref('')
    const saveStatus = ref(null)
    const originalPages = ref(null)

    const tabs = ['Pages', 'Subpages']

    // Initialize JSON editors
    watch(
      () => props.pages,
      (newVal) => {
        if (newVal && newVal.pages) {
          editedPagesJson.value = JSON.stringify(newVal.pages, null, 2)
          originalPages.value = JSON.stringify(newVal.pages)
        }
        if (newVal && newVal.subpages) {
          editedSubpagesJson.value = JSON.stringify(newVal.subpages, null, 2)
        }
      },
      { immediate: true, deep: true }
    )

    function validateJson() {
      try {
        const pagesData = JSON.parse(editedPagesJson.value)
        const subpagesData = JSON.parse(editedSubpagesJson.value)
        jsonError.value = ''
        return { pages: pagesData, subpages: subpagesData }
      } catch (err) {
        jsonError.value = `JSON Error: ${err.message}`
        return null
      }
    }

    function onJsonChange() {
      validateJson()
    }

    function handleSave() {
      const validated = validateJson()
      if (!validated) return

      // Emit save event with updated data
      emit('save', validated)

      // Show success message
      saveStatus.value = {
        type: 'success',
        message: '✓ Changes saved'
      }

      // Clear message after 3 seconds
      setTimeout(() => {
        saveStatus.value = null
      }, 3000)
    }

    function handleReset() {
      if (originalPages.value) {
        editedPagesJson.value = originalPages.value
        jsonError.value = ''
      }
    }

    return {
      activeTab,
      tabs,
      editedPagesJson,
      editedSubpagesJson,
      jsonError,
      saveStatus,
      onJsonChange,
      handleSave,
      handleReset
    }
  }
}
</script>

<style scoped>
.page-editor {
  display: flex;
  flex-direction: column;
  height: 100%;
  color: var(--text-main);
}

/* ── Tab strip (matches MM's flat tab bar) ── */
.editor-tabs {
  display: flex;
  background: var(--bg-shell);
  border-bottom: 1px solid var(--border-soft);
  padding: 0 8px;
  flex-shrink: 0;
}

.tab {
  padding: 0 16px;
  height: 32px;
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

.tab.active {
  color: var(--accent);
  border-bottom-color: var(--accent);
}

/* ── Content area ── */
.editor-content,
.pages-section,
.subpages-section {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  padding: 12px;
  gap: 8px;
}

.pages-section h3,
.subpages-section h3 {
  margin: 0 0 4px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--border-soft);
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: var(--text-dim);
}

/* ── JSON textarea ── */
.json-editor {
  flex: 1;
  display: flex;
  min-height: 0;
}

.json-editor textarea {
  flex: 1;
  padding: 10px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  color: var(--text-main);
  font-family: "SFMono-Regular", "Menlo", monospace;
  font-size: 11px;
  line-height: 1.55;
  resize: none;
  background: var(--bg-app);
}

.json-editor textarea:focus {
  outline: none;
  border-color: var(--accent-dim);
}

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

/* ── Action buttons ── */
.button-group {
  display: flex;
  gap: 6px;
  flex-shrink: 0;
}

.save-btn,
.reset-btn {
  padding: 5px 14px;
  border-radius: var(--radius-xs);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.1s, opacity 0.1s;
}

.save-btn {
  border: 1px solid var(--accent-dim);
  background: var(--accent);
  color: #071316;
  flex: 1;
}

.save-btn:hover:not(:disabled) { background: var(--accent-strong); }
.save-btn:disabled { opacity: 0.4; cursor: not-allowed; }

.reset-btn {
  border: 1px solid var(--border-soft);
  background: var(--bg-panel-soft);
  color: var(--text-muted);
}

.reset-btn:hover { background: var(--bg-hover); color: var(--text-main); }

.save-status { animation: fadeIn 0.15s ease-out; }

@keyframes fadeIn {
  from { opacity: 0; }
  to   { opacity: 1; }
}

@media (max-width: 700px) {
  .save-btn, .reset-btn { width: 100%; }
}
</style>
