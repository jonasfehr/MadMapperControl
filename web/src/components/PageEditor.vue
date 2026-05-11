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
  padding: 1.5rem;
  gap: 1rem;
}

.editor-tabs {
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

.editor-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.pages-section,
.subpages-section {
  display: flex;
  flex-direction: column;
  flex: 1;
  overflow: hidden;
}

.pages-section h3,
.subpages-section h3 {
  margin: 0 0 1rem 0;
  font-size: 1rem;
  color: #333;
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

.save-btn,
.reset-btn {
  padding: 0.75rem 1.5rem;
  border: none;
  border-radius: 4px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.save-btn {
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
