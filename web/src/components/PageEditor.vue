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
  padding: 1rem;
  gap: 1rem;
  color: var(--text-main);
}

.editor-tabs {
  display: inline-flex;
  gap: 0.45rem;
  align-self: flex-start;
  padding: 0.35rem;
  border: 1px solid var(--border-strong);
  border-radius: 14px;
  background: linear-gradient(180deg, rgba(56, 56, 56, 0.9), rgba(37, 37, 37, 0.96));
}

.tab {
  min-width: 120px;
  padding: 0.7rem 1rem;
  border: 1px solid transparent;
  border-radius: 10px;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
  font-weight: 700;
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

.editor-content,
.pages-section,
.subpages-section {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
}

.pages-section h3,
.subpages-section h3 {
  margin: 0 0 0.9rem 0;
  padding-bottom: 0.7rem;
  border-bottom: 1px solid var(--border-soft);
  font-size: 0.96rem;
  color: var(--text-main);
}

.json-editor {
  flex: 1;
  display: flex;
  min-height: 0;
}

.json-editor textarea {
  flex: 1;
  padding: 1rem;
  border: 1px solid var(--border-soft);
  border-radius: 14px;
  color: var(--text-main);
  font-family: "SFMono-Regular", "Menlo", monospace;
  font-size: 0.84rem;
  resize: none;
  background: linear-gradient(180deg, rgba(35, 35, 35, 0.95), rgba(28, 28, 28, 0.98));
}

.json-editor textarea:focus {
  outline: none;
  border-color: rgba(24, 200, 218, 0.55);
  box-shadow: 0 0 0 3px rgba(24, 200, 218, 0.12);
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

.button-group {
  display: flex;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.save-btn,
.reset-btn {
  padding: 0.75rem 1rem;
  border-radius: 10px;
  font-weight: 700;
  cursor: pointer;
  transition: background 0.18s ease, border-color 0.18s ease, color 0.18s ease, transform 0.18s ease;
}

.save-btn {
  border: 1px solid rgba(24, 200, 218, 0.35);
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  color: #061518;
  flex: 1;
}

.save-btn:hover:not(:disabled),
.reset-btn:hover {
  transform: translateY(-1px);
}

.save-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
}

.reset-btn {
  border: 1px solid var(--border-soft);
  background: linear-gradient(180deg, rgba(73, 73, 73, 0.94), rgba(56, 56, 56, 0.94));
  color: var(--text-main);
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

@media (max-width: 700px) {
  .save-btn,
  .reset-btn,
  .tab {
    width: 100%;
  }
}
  background: #667eea;
  color: white;
  flex: 1;
}

.save-btn:hover:not(:disabled) {
  background: #764ba2;
}

.save-btn:disabled {
  background: #ccc;
  cursor: not-allowed;
}

.reset-btn {
  background: #f5f5f5;
  color: #333;
  border: 1px solid #e0e0e0;
}

.reset-btn:hover {
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
