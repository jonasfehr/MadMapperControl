<template>
  <div id="app" class="app-container">

    <!-- ── Toolbar (MM-style icon bar) ── -->
    <div class="toolbar">
      <div class="toolbar-tabs">

        <!-- Pages -->
        <button
          class="toolbar-btn"
          :class="{ active: activeView === 'pages' }"
          @click="activeView = 'pages'"
          title="Pages"
        >
          <svg class="tb-icon" viewBox="0 0 20 20" fill="none">
            <rect x="3" y="3" width="14" height="2" rx="1" fill="currentColor"/>
            <rect x="3" y="7" width="10" height="2" rx="1" fill="currentColor" opacity="0.7"/>
            <rect x="3" y="11" width="12" height="2" rx="1" fill="currentColor" opacity="0.5"/>
            <rect x="3" y="15" width="7"  height="2" rx="1" fill="currentColor" opacity="0.3"/>
          </svg>
          <span class="tb-label">Pages</span>
        </button>

        <!-- Endpoints -->
        <button
          class="toolbar-btn"
          :class="{ active: activeView === 'endpoints' }"
          @click="activeView = 'endpoints'"
          title="Endpoints"
        >
          <svg class="tb-icon" viewBox="0 0 20 20" fill="none">
            <circle cx="10" cy="10" r="2.5" fill="currentColor"/>
            <circle cx="3"  cy="4"  r="2"   fill="currentColor" opacity="0.7"/>
            <circle cx="17" cy="4"  r="2"   fill="currentColor" opacity="0.7"/>
            <circle cx="3"  cy="16" r="2"   fill="currentColor" opacity="0.5"/>
            <circle cx="17" cy="16" r="2"   fill="currentColor" opacity="0.5"/>
            <line x1="10" y1="10" x2="3"  y2="4"  stroke="currentColor" stroke-width="1.2" opacity="0.5"/>
            <line x1="10" y1="10" x2="17" y2="4"  stroke="currentColor" stroke-width="1.2" opacity="0.5"/>
            <line x1="10" y1="10" x2="3"  y2="16" stroke="currentColor" stroke-width="1.2" opacity="0.4"/>
            <line x1="10" y1="10" x2="17" y2="16" stroke="currentColor" stroke-width="1.2" opacity="0.4"/>
          </svg>
          <span class="tb-label">Endpoints</span>
        </button>

        <!-- Emulator -->
        <button
          class="toolbar-btn"
          :class="{ active: activeView === 'emulator' }"
          @click="activeView = 'emulator'"
          title="Device Emulator"
        >
          <svg class="tb-icon" viewBox="0 0 20 20" fill="none">
            <!-- fader 1: track + handle -->
            <line x1="5"  y1="3" x2="5"  y2="17" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" opacity="0.45"/>
            <rect x="2.5" y="6.5" width="5" height="3.5" rx="1" fill="currentColor" opacity="0.9"/>
            <!-- fader 2 -->
            <line x1="10" y1="3" x2="10" y2="17" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" opacity="0.45"/>
            <rect x="7.5" y="11" width="5" height="3.5" rx="1" fill="currentColor" opacity="0.65"/>
            <!-- encoder knob -->
            <circle cx="16" cy="7.5" r="3.2" stroke="currentColor" stroke-width="1.2" opacity="0.8"/>
            <line x1="16" y1="7.5" x2="16" y2="4.8" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" opacity="0.9"/>
            <!-- small display strip -->
            <rect x="13.5" y="12" width="5" height="3" rx="0.8" stroke="currentColor" stroke-width="0.9" opacity="0.45"/>
          </svg>
          <span class="tb-label">Emulator</span>
        </button>
      </div>

      <!-- Right side: server status + live page -->
      <div class="toolbar-right">
        <template v-for="(cfg, i) in (config?.servers || [])" :key="i">
          <div class="server-chip" :title="cfg.ip + ':' + cfg.queryPort">
            <span class="server-dot" :class="serverConnected(i) ? 'on' : 'off'"></span>
            {{ cfg.id }}
          </div>
        </template>
        <span v-if="config?.currentPage" class="live-chip">
          <span class="live-dot"></span>{{ config.currentPage }}
        </span>
      </div>
    </div>

    <!-- ── Main content ── -->
    <main class="main-content">
      <div v-if="loading" class="state-overlay">Loading…</div>
      <div v-else-if="error" class="state-overlay error">
        <p>{{ error }}</p>
        <button @click="loadData">Retry</button>
      </div>
      <template v-else>
        <section v-show="activeView === 'pages'" class="main-panel">
          <PageManager
            :pages="pages"
            :allParameters="allParameterEntries"
            @save="savePages"
            @activate-page="activatePage"
          />
        </section>
        <section v-show="activeView === 'endpoints'" class="main-panel">
          <EndpointConfig
            :config="config || {}"
            :announced-endpoints="bonjourAnnouncements"
            @save-config="saveConfig"
          />
        </section>
        <section v-show="activeView === 'emulator'" class="main-panel">
          <MockDevicePanel
            :pages="pages"
            :config="config"
            :all-parameters="allParameterEntries"
          />
        </section>
      </template>
    </main>

  </div>
</template>

<script>
import { reactive, ref, computed, onMounted } from 'vue'
import { apiClient } from './api'
import PageManager from './components/PageManager.vue'
import EndpointConfig from './components/EndpointConfig.vue'
import MockDevicePanel from './components/mock/MockDevicePanel.vue'

export default {
  name: 'App',
  components: {
    PageManager,
    EndpointConfig,
    MockDevicePanel
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

          const rawVal = param && typeof param === 'object' ? param.value : undefined
          const value = Array.isArray(rawVal) ? (rawVal[0] ?? null) : (rawVal ?? null)
          entries.push({
            key,
            name: name || path,
            label,
            path: fullPath,
            serverId,
            serverName,
            value
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

    function serverConnected(idx) {
      const serverId = `server_${idx}`
      const serverData = allServers[serverId]
      return serverData && serverData.connected
    }

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
      saveConfig,
      serverConnected
    }
  }
}
</script>

<style scoped>
* { box-sizing: border-box; }

/* ── Root layout ── */
.app-container {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: var(--bg-app);
  overflow: hidden;
}

/* ── Toolbar — MM-style icon bar ── */
.toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 48px;
  padding: 0 10px;
  flex-shrink: 0;
  background: var(--bg-shell);
  border-bottom: 1px solid var(--border-strong);
  gap: 8px;
}

.toolbar-tabs {
  display: flex;
  align-items: center;
  gap: 4px;
}

/* ── Toolbar button — active expands with label ── */
.toolbar-btn {
  display: flex;
  align-items: center;
  gap: 7px;
  height: 36px;
  padding: 0 10px;
  border: 1px solid transparent;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 12px;
  font-weight: 500;
  letter-spacing: 0.01em;
  white-space: nowrap;
  transition: color 0.1s, background 0.1s, border-color 0.1s;
}

.toolbar-btn:hover:not(:disabled) {
  background: var(--bg-hover);
  color: var(--text-muted);
  border-color: var(--border-soft);
}

.toolbar-btn.active {
  background: var(--bg-active);
  border-color: var(--accent-dim);
  color: var(--accent);
}

/* Hide label when inactive — icon only */
.toolbar-btn:not(.active) .tb-label { display: none; }

.toolbar-btn--soon {
  opacity: 0.38;
  cursor: default;
}
/* Show "soon" label always for the disabled button */
.toolbar-btn--soon .tb-label { display: inline; }

.tb-icon {
  width: 18px; height: 18px;
  flex-shrink: 0;
  color: inherit;
}

.tb-label {
  font-size: 12px;
  font-weight: 600;
  color: inherit;
}

.soon-pill {
  font-size: 9px;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-dim);
  background: var(--bg-panel-soft);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  padding: 1px 4px;
  margin-left: 2px;
}

/* ── Toolbar right — server chips + live page ── */
.toolbar-right {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-left: auto;
  flex-shrink: 0;
}

.server-chip {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 2px 8px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  font-size: 10px;
  font-weight: 600;
  color: var(--text-dim);
  letter-spacing: 0.04em;
  background: var(--bg-panel-soft);
}

.server-dot {
  width: 5px; height: 5px;
  border-radius: 50%;
  flex-shrink: 0;
}
.server-dot.on  { background: #4caf50; }
.server-dot.off { background: #484848; }

.live-chip {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 2px 8px;
  border: 1px solid var(--accent-dim);
  border-radius: var(--radius-xs);
  font-size: 11px;
  color: var(--accent);
  background: var(--bg-active);
}

.live-dot {
  width: 5px; height: 5px;
  border-radius: 50%;
  background: var(--accent);
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0.3; }
}

/* ── Main content ── */
.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--bg-panel);
}

.main-panel {
  flex: 1;
  width: 100%;
  overflow: auto;
  display: flex;
  flex-direction: column;
}

/* ── Loading / error states ── */
.state-overlay {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  flex: 1;
  color: var(--text-muted);
  font-size: 11px;
}

.state-overlay.error p { color: var(--error-text); margin: 0; }

.state-overlay button {
  padding: 5px 14px;
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-xs);
  color: var(--text-main);
  background: var(--bg-panel-soft);
  cursor: pointer;
  font-size: 11px;
  font-weight: 600;
}
</style>
