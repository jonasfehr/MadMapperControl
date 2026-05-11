<template>
  <div id="app" class="app-container">
    <header class="header">
      <h1>MadMapper Controller Config</h1>
      <div class="header-info">
        <span v-if="config" class="status">
          Status: {{ config.initialized ? '✓ Ready' : '⏳ Loading' }}
        </span>
      </div>
    </header>

    <main class="main-content">
      <div v-if="loading" class="loading">Loading...</div>

      <div v-else-if="error" class="error">
        <p>Error: {{ error }}</p>
        <button @click="loadData">Retry</button>
      </div>

      <div v-else class="editor-layout">
        <!-- Left sidebar: Parameter tree -->
        <aside class="sidebar parameters-panel">
          <h2>Parameters</h2>
          <div class="search">
            <input
              v-model="parameterSearch"
              type="text"
              placeholder="Search parameters..."
            />
          </div>
          <div class="param-tree">
            <ul v-if="filteredParameters.length">
              <li v-for="(param, idx) in filteredParameters" :key="idx">
                {{ param }}
              </li>
            </ul>
            <p v-else class="empty">No parameters found</p>
          </div>
        </aside>

        <!-- Main content: Page manager -->
        <section class="main-panel">
          <PageManager :pages="pages" :allParameters="allParameterEntries" @save="savePages" />
        </section>
      </div>
    </main>
  </div>
</template>

<script>
import { reactive, ref, computed, onMounted } from 'vue'
import { apiClient } from './api'
import PageManager from './components/PageManager.vue'

export default {
  name: 'App',
  components: {
    PageManager
  },
  setup() {
    const pages = reactive({ pages: [], subpages: [] })
    const allServers = reactive({}) // Store all server parameters keyed by server_id
    const config = ref(null)
    const loading = ref(true)
    const error = ref(null)
    const parameterSearch = ref('')

    function wildcardToRegExp(pattern) {
      const escaped = pattern.replace(/[.+?^${}()|[\]\\]/g, '\\$&')
      return new RegExp(`^${escaped.replace(/\*/g, '.*')}$`, 'i')
    }

    const allParameterEntries = computed(() => {
      const entries = []
      const seen = new Set()

      Object.entries(allServers).forEach(([serverId, server]) => {
        if (!server || !Array.isArray(server.parameters)) return
        const serverName = server.name || serverId

        server.parameters.forEach(param => {
          let name = ''
          let path = ''

          if (typeof param === 'string') {
            name = param
            path = param
          } else if (param && typeof param === 'object') {
            name = param.display || param.name || param.path || ''
            path = param.path || param.name || ''
          }

          if (!name && !path) return

          const fullPath = path || name
          const label = `${fullPath} [${serverName}]`
          const key = `${serverId}::${path || name}`
          if (seen.has(key)) return
          seen.add(key)

          entries.push({
            key,
            name: name || path,
            label,
            path: fullPath,
            serverId,
            serverName
          })
        })
      })

      return entries
    })

    const filteredParameters = computed(() => {
      const query = parameterSearch.value.trim()
      if (!query) return allParameterEntries.value.map(p => p.label)

      const hasWildcard = query.includes('*')
      const regex = hasWildcard ? wildcardToRegExp(query) : null
      return allParameterEntries.value
        .filter(p => {
          const haystack = `${p.label} ${p.path}`
          if (regex) return regex.test(p.path) || regex.test(haystack)
          return haystack.toLowerCase().includes(query.toLowerCase())
        })
        .map(p => p.label)
    })

    async function loadData() {
      try {
        loading.value = true
        error.value = null

        // Load all data in parallel
        const [pagesData, paramsData, configData] = await Promise.all([
          apiClient.fetchPages(),
          apiClient.fetchParameters(),
          apiClient.fetchConfig()
        ])

        Object.assign(pages, pagesData)
        Object.assign(allServers, paramsData)
        config.value = configData
      } catch (err) {
        console.error('Failed to load data:', err)
        error.value = err.message || 'Failed to load configuration'
      } finally {
        loading.value = false
      }
    }

    async function savePages(updatedPages) {
      try {
        await apiClient.savePages(updatedPages)
        Object.assign(pages, updatedPages)
        console.log('Pages saved successfully')
      } catch (err) {
        console.error('Failed to save pages:', err)
        error.value = err.message || 'Failed to save pages'
      }
    }

    onMounted(() => {
      loadData()
      // Refresh config every 5 seconds
      setInterval(() => {
        apiClient.fetchConfig().then(data => (config.value = data))
      }, 5000)
    })

    return {
      pages,
      allServers,
      allParameterEntries,
      config,
      loading,
      error,
      parameterSearch,
      filteredParameters,
      loadData,
      savePages
    }
  }
}
</script>

<style scoped>
* {
  box-sizing: border-box;
}

body,
html {
  margin: 0;
  padding: 0;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen,
    Ubuntu, Cantarell, sans-serif;
  background: #f5f5f5;
}

.app-container {
  display: flex;
  flex-direction: column;
  height: 100vh;
}

.header {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  padding: 1.5rem 2rem;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header h1 {
  margin: 0;
  font-size: 1.5rem;
}

.header-info {
  display: flex;
  gap: 1rem;
  align-items: center;
}

.status {
  font-size: 0.9rem;
  background: rgba(255, 255, 255, 0.2);
  padding: 0.5rem 1rem;
  border-radius: 4px;
}

.main-content {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.loading {
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.2rem;
  color: #666;
  flex: 1;
}

.error {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  flex: 1;
  gap: 1rem;
}

.error p {
  color: #d32f2f;
  margin: 0;
}

.error button {
  padding: 0.5rem 1rem;
  background: #667eea;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
}

.error button:hover {
  background: #764ba2;
}

.editor-layout {
  display: flex;
  flex: 1;
  gap: 0;
  overflow: hidden;
}

.sidebar {
  width: 250px;
  background: white;
  border-right: 1px solid #e0e0e0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.sidebar h2 {
  margin: 0;
  padding: 1rem;
  border-bottom: 1px solid #e0e0e0;
  font-size: 1rem;
}

.search {
  padding: 0.5rem;
}

.search input {
  width: 100%;
  padding: 0.5rem 0.75rem;
  border: 1px solid #e0e0e0;
  border-radius: 4px;
  font-size: 0.9rem;
}

.param-tree {
  flex: 1;
  overflow-y: auto;
  padding: 0.5rem;
}

.param-tree ul {
  list-style: none;
  margin: 0;
  padding: 0;
}

.param-tree li {
  padding: 0.5rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
  word-break: break-all;
  transition: background 0.2s;
}

.param-tree li:hover {
  background: #f0f0f0;
}

.empty {
  color: #999;
  font-size: 0.9rem;
  text-align: center;
  padding: 1rem;
}

.main-panel {
  flex: 1;
  background: white;
  overflow: auto;
}
</style>
