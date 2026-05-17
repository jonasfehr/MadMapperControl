<template>
  <div class="endpoint-config">
    <div class="panel-header">
      <h3>Endpoint Configuration</h3>
      <p>Manuelle Endpoints und Bonjour-Hosts (.local) konfigurieren.</p>
    </div>

    <div class="endpoint-list">
      <div v-for="(server, idx) in draftServers" :key="server.localKey" class="endpoint-card">
        <div class="card-head">
          <strong>Endpoint {{ idx + 1 }}</strong>
          <span class="reachability" :class="{ up: server.reachable, down: !server.reachable }">
            {{ server.reachable ? 'reachable' : 'unreachable' }}
          </span>
        </div>

        <div class="grid">
          <label>
            Name
            <input v-model="server.id" type="text" placeholder="MadMapper" />
          </label>
          <label>
            Host / IP
            <input v-model="server.ip" type="text" placeholder="127.0.0.1 oder madmapper.local" />
          </label>
          <label>
            Query Port
            <input v-model.number="server.queryPort" type="number" min="1" max="65535" />
          </label>
          <label>
            Send Port
            <input v-model.number="server.sendPort" type="number" min="1" max="65535" />
          </label>
          <label>
            Feedback Port
            <input v-model.number="server.feedbackPort" type="number" min="1" max="65535" />
          </label>
          <label>
            Source
            <select v-model="server.discovery">
              <option value="manual">Manual</option>
              <option value="bonjour">Bonjour</option>
            </select>
          </label>
        </div>

        <div class="actions">
          <button class="danger" @click="removeServer(idx)">Remove</button>
        </div>
      </div>
    </div>

    <div class="bonjour-section" v-if="availableAnnouncements.length">
      <h4>Bonjour Announcements</h4>
      <p>Gefundene Endpoints koennen direkt zur Konfiguration hinzugefuegt werden.</p>
      <div class="announcement-list">
        <div v-for="item in availableAnnouncements" :key="item.localKey" class="announcement-item">
          <div>
            <strong>{{ item.id }}</strong>
            <div class="announcement-meta">{{ item.ip }}:{{ item.queryPort }}</div>
          </div>
          <button class="secondary" @click="addAnnounced(item)">Add to config</button>
        </div>
      </div>
    </div>

    <div class="footer-actions">
      <button class="secondary" @click="addManual">+ Add Endpoint</button>
      <button class="primary" @click="save" :disabled="saving">{{ saving ? 'Saving...' : 'Save Endpoints' }}</button>
    </div>

    <div v-if="status" class="status" :class="status.type">{{ status.message }}</div>
  </div>
</template>

<script>
import { computed, ref, watch } from 'vue'

export default {
  name: 'EndpointConfig',
  props: {
    config: {
      type: Object,
      default: () => ({ servers: [] })
    },
    announcedEndpoints: {
      type: Array,
      default: () => []
    }
  },
  emits: ['save-config'],
  setup(props, { emit }) {
    const draftServers = ref([])
    const saving = ref(false)
    const status = ref(null)

    function normalizeServer(server, idx) {
      return {
        localKey: `${Date.now()}_${idx}_${Math.random().toString(16).slice(2)}`,
        id: server?.id || `server_${idx}`,
        ip: server?.ip || '127.0.0.1',
        queryPort: Number(server?.queryPort || 9001),
        sendPort: Number(server?.sendPort || server?.queryPort || 9001),
        feedbackPort: Number(server?.feedbackPort || 9893),
        discovery: server?.discovery === 'bonjour' ? 'bonjour' : 'manual',
        reachable: Boolean(server?.reachable)
      }
    }

    watch(
      () => props.config,
      (cfg) => {
        const source = Array.isArray(cfg?.servers) ? cfg.servers : []
        draftServers.value = source.map((s, idx) => normalizeServer(s, idx))
      },
      { immediate: true, deep: true }
    )

    function addManual() {
      draftServers.value.push(normalizeServer({ id: 'New Endpoint' }, draftServers.value.length))
    }

    const availableAnnouncements = computed(() => {
      const byHostPort = new Set(
        draftServers.value.map(s => `${String(s.ip).trim().toLowerCase()}:${Number(s.queryPort)}`)
      )
      return (props.announcedEndpoints || [])
        .map((item, idx) => ({
          localKey: `ann_${idx}_${String(item.ip || '').toLowerCase()}_${Number(item.queryPort || 0)}`,
          id: item.id || `bonjour_${idx + 1}`,
          ip: item.ip || '',
          queryPort: Number(item.queryPort || 9001),
          sendPort: Number(item.sendPort || item.queryPort || 9001),
          feedbackPort: Number(item.feedbackPort || 9893),
          discovery: 'bonjour'
        }))
        .filter(item => item.ip && !byHostPort.has(`${String(item.ip).trim().toLowerCase()}:${Number(item.queryPort)}`))
    })

    function addAnnounced(item) {
      draftServers.value.push(
        normalizeServer(
          {
            id: item.id,
            ip: item.ip,
            queryPort: item.queryPort,
            sendPort: item.sendPort,
            feedbackPort: item.feedbackPort,
            discovery: 'bonjour'
          },
          draftServers.value.length
        )
      )
    }

    function removeServer(index) {
      draftServers.value.splice(index, 1)
    }

    function toPayload() {
      return {
        servers: draftServers.value
          .map((server, idx) => ({
            id: (server.id || `server_${idx}`).trim(),
            ip: (server.ip || '').trim(),
            queryPort: Number(server.queryPort),
            sendPort: Number(server.sendPort),
            feedbackPort: Number(server.feedbackPort),
            discovery: server.discovery === 'bonjour' ? 'bonjour' : 'manual'
          }))
          .filter(server => server.id && server.ip && server.queryPort > 0 && server.sendPort > 0 && server.feedbackPort > 0)
      }
    }

    function save() {
      const payload = toPayload()
      if (!payload.servers.length) {
        status.value = { type: 'error', message: 'Mindestens ein valider Endpoint ist erforderlich.' }
        return
      }

      saving.value = true
      emit('save-config', payload)
      status.value = { type: 'success', message: 'Endpoints gespeichert. Reconnect wird angewendet.' }
      setTimeout(() => {
        saving.value = false
      }, 350)
    }

    return {
      draftServers,
      saving,
      status,
      addManual,
      addAnnounced,
      availableAnnouncements,
      removeServer,
      save
    }
  }
}
</script>

<style scoped>
.endpoint-config {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding: 1rem;
}

.panel-header h3 {
  margin: 0;
}

.panel-header p {
  margin: 0.35rem 0 0 0;
  color: var(--text-dim);
}

.bonjour-section {
  border: 1px solid var(--border-soft);
  border-radius: 14px;
  padding: 0.85rem;
  background: linear-gradient(180deg, rgba(39, 39, 39, 0.98), rgba(30, 30, 30, 0.98));
}

.bonjour-section h4 {
  margin: 0;
}

.bonjour-section p {
  margin: 0.3rem 0 0.75rem 0;
  color: var(--text-dim);
  font-size: 0.82rem;
}

.announcement-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.announcement-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.75rem;
  border: 1px solid var(--border-soft);
  border-radius: 10px;
  padding: 0.6rem 0.7rem;
  background: linear-gradient(180deg, rgba(55, 55, 55, 0.96), rgba(42, 42, 42, 0.96));
}

.announcement-meta {
  color: var(--text-dim);
  font-size: 0.78rem;
}

.endpoint-list {
  display: flex;
  flex-direction: column;
  gap: 0.8rem;
  max-height: 62vh;
  overflow: auto;
}

.endpoint-card {
  border: 1px solid var(--border-soft);
  border-radius: 14px;
  padding: 0.9rem;
  background: linear-gradient(180deg, rgba(44, 44, 44, 0.98), rgba(31, 31, 31, 0.98));
}

.card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 0.7rem;
}

.reachability {
  padding: 0.25rem 0.55rem;
  border-radius: 999px;
  font-size: 0.75rem;
  border: 1px solid transparent;
}

.reachability.up {
  color: var(--success-text);
  background: var(--success-bg);
  border-color: rgba(46, 125, 50, 0.35);
}

.reachability.down {
  color: var(--error-text);
  background: var(--error-bg);
  border-color: rgba(198, 40, 40, 0.35);
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0.7rem;
}

label {
  display: flex;
  flex-direction: column;
  gap: 0.35rem;
  color: var(--text-muted);
  font-size: 0.84rem;
}

input,
select {
  width: 100%;
  border: 1px solid var(--border-soft);
  border-radius: 10px;
  padding: 0.6rem 0.75rem;
  color: var(--text-main);
  background: var(--bg-input);
}

input:focus,
select:focus {
  outline: none;
  border-color: rgba(24, 200, 218, 0.55);
  box-shadow: 0 0 0 3px rgba(24, 200, 218, 0.12);
}

.actions {
  display: flex;
  justify-content: flex-end;
  margin-top: 0.8rem;
}

.footer-actions {
  display: flex;
  gap: 0.6rem;
  flex-wrap: wrap;
}

button {
  border-radius: 10px;
  padding: 0.65rem 0.95rem;
  border: 1px solid var(--border-soft);
  background: linear-gradient(180deg, rgba(73, 73, 73, 0.94), rgba(56, 56, 56, 0.94));
  color: var(--text-main);
  cursor: pointer;
  font-weight: 700;
}

button.primary {
  border-color: rgba(24, 200, 218, 0.35);
  background: linear-gradient(180deg, var(--accent), var(--accent-strong));
  color: #061518;
}

button.danger {
  border-color: rgba(198, 40, 40, 0.35);
  background: rgba(198, 40, 40, 0.2);
  color: var(--error-text);
}

button:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.status {
  padding: 0.75rem 0.9rem;
  border-radius: 12px;
  border: 1px solid transparent;
  font-size: 0.84rem;
  font-weight: 700;
}

.status.success {
  color: var(--success-text);
  background: var(--success-bg);
  border-color: rgba(46, 125, 50, 0.28);
}

.status.error {
  color: var(--error-text);
  background: var(--error-bg);
  border-color: rgba(198, 40, 40, 0.34);
}

@media (max-width: 900px) {
  .grid {
    grid-template-columns: 1fr;
  }
}
</style>
