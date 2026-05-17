<template>
  <div id="app" class="app-container">
    <header class="header">
      <div class="title-group">
        <span class="eyebrow">MadMapper Controller</span>
        <h1>Page Config</h1>
      </div>
      <div class="header-info">
        <span v-if="config" class="status">
          Status: {{ config.initialized ? '✓ Ready' : '⏳ Loading' }}
        </span>
        <span v-if="config?.currentPage" class="status current-page">
          Live: {{ config.currentPage }}
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
        <div class="view-tabs">
          <button class="view-tab" :class="{ active: activeView === 'pages' }" @click="activeView = 'pages'">
            Pages
          </button>
          <button class="view-tab" :class="{ active: activeView === 'endpoints' }" @click="activeView = 'endpoints'">
            Endpoints
          </button>
        </div>

        <!-- Main content: Page manager -->
        <section v-if="activeView === 'pages'" class="main-panel full-width">
          <PageManager
            :pages="pages"
            :allParameters="allParameterEntries"
            @save="savePages"
            @activate-page="activatePage"
          />
        </section>

        <section v-else class="main-panel full-width">
          <EndpointConfig
            :config="config || {}"
            :announced-endpoints="bonjourAnnouncements"
            @save-config="saveConfig"
          />
        </section>
      </div>
    </main>
  </div>
</template>

<script>
import { reactive, ref, computed, onMounted } from 'vue'
import { apiClient } from './api'
import PageManager from './components/PageManager.vue'
import EndpointConfig from './components/EndpointConfig.vue'

export default {
  name: 'App',
  components: {
    PageManager,
    EndpointConfig
  },
  setup() {
    const pages = reactive({ pages: [], subpages: [] })
    const allServers = reactive({}) // Store all server parameters keyed by server_id
    const config = ref(null)
    const loading = ref(true)
    const error = ref(null)
    const parameterSearch = ref('')
    const activeView = ref('pages')

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

    const bonjourAnnouncements = computed(() => {
      const announced = Array.isArray(config.value?.bonjourAnnouncements)
        ? config.value.bonjourAnnouncements
        : []
      if (announced.length) return announced

      const servers = Array.isArray(config.value?.servers) ? config.value.servers : []
      return servers
        .filter(server => server && server.discovery === 'bonjour')
        .map(server => ({
          id: server.id,
          ip: server.ip,
          queryPort: server.queryPort,
          sendPort: server.sendPort,
          feedbackPort: server.feedbackPort
        }))
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

    async function activatePage(pageName) {
      try {
        await apiClient.activatePage(pageName)
        config.value = { ...(config.value || {}), currentPage: pageName }
      } catch (err) {
        console.error('Failed to activate page:', err)
        error.value = err.message || 'Failed to activate page'
      }
    }

    async function saveConfig(updatedConfig) {
      try {
        await apiClient.saveConfig(updatedConfig)
        await loadData()
      } catch (err) {
        console.error('Failed to save config:', err)
        error.value = err.message || 'Failed to save config'
        throw err
      }
    }

    onMounted(() => {
      loadData()
      // Refresh config every 5 seconds
      setInterval(() => {
        if (activeView.value === 'endpoints') return
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
      activeView,
      parameterSearch,
      filteredParameters,
      bonjourAnnouncements,
      loadData,
      savePages,
      activatePage,
      saveConfig
    }
  }
}
</script>

<style scoped>
* {
  box-sizing: border-box;
}

.app-container {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
  padding: 0.75rem;
  gap: 0.75rem;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 1rem;
  padding: 0.9rem 1.2rem;
  border: 1px solid var(--border-strong);
  border-radius: 18px;
  background: linear-gradient(180deg, rgba(45, 45, 45, 0.96), rgba(27, 27, 27, 0.96));
  box-shadow: var(--shadow-panel);
}

.title-group {
  display: flex;
  flex-direction: column;
  gap: 0.1rem;
}

.eyebrow {
  font-size: 0.72rem;
  letter-spacing: 0.18em;
  text-transform: uppercase;
  color: var(--accent);
}

.header h1 {
  margin: 0;
  font-size: 1.4rem;
  line-height: 1.1;
  font-weight: 700;
  color: var(--text-main);
}

.header-info {
  display: flex;
  gap: 0.6rem;
  align-items: center;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.status {
  font-size: 0.82rem;
  padding: 0.45rem 0.8rem;
  border-radius: 999px;
  color: var(--text-main);
  border: 1px solid var(--border-soft);
  background: linear-gradient(180deg, rgba(62, 62, 62, 0.95), rgba(47, 47, 47, 0.95));
}

.current-page {
  border-color: rgba(24, 200, 218, 0.42);
  box-shadow: inset 0 0 0 1px rgba(24, 200, 218, 0.15);
}

.main-content {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.loading,
.error {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1rem;
  flex: 1;
  border: 1px solid var(--border-strong);
  border-radius: 20px;
  background: linear-gradient(180deg, rgba(40, 40, 40, 0.96), rgba(28, 28, 28, 0.96));
  box-shadow: var(--shadow-panel);
}

.loading {
  color: var(--text-muted);
  font-size: 1rem;
}

.error p {
  color: var(--error-text);
  margin: 0;
}

.error button {
  padding: 0.7rem 1.2rem;
  border: 1px solid rgba(24, 200, 218, 0.35);
  border-radius: 10px;
  color: #071316;
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  cursor: pointer;
  font-weight: 700;
}

.editor-layout {
  display: flex;
  flex-direction: column;
  gap: 0.65rem;
  flex: 1;
  overflow: hidden;
}

.view-tabs {
  display: inline-flex;
  gap: 0.45rem;
  align-self: flex-start;
  padding: 0.35rem;
  border: 1px solid var(--border-strong);
  border-radius: 14px;
  background: linear-gradient(180deg, rgba(56, 56, 56, 0.9), rgba(37, 37, 37, 0.96));
}

.view-tab {
  min-width: 120px;
  padding: 0.6rem 0.9rem;
  border: 1px solid transparent;
  border-radius: 10px;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
  font-weight: 700;
}

.view-tab.active {
  color: #061518;
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  border-color: rgba(24, 200, 218, 0.35);
}

.main-panel {
  flex: 1;
  width: 100%;
  overflow: auto;
  border: 1px solid var(--border-strong);
  border-radius: 22px;
  background: linear-gradient(180deg, rgba(36, 36, 36, 0.98), rgba(26, 26, 26, 0.98));
  box-shadow: var(--shadow-panel);
}

.main-panel.full-width {
  width: 100%;
}

@media (max-width: 900px) {
  .app-container {
    padding: 0.5rem;
  }

  .header {
    flex-direction: column;
    align-items: flex-start;
  }

  .header-info {
    justify-content: flex-start;
  }
}
</style>
