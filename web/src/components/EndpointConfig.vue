<template>
  <div class="endpoint-config">

    <div class="section-header">Endpoints</div>

    <div class="endpoint-list">
      <div v-for="(server, idx) in draftServers" :key="server.localKey" class="endpoint-card">
        <div class="card-head">
          <span class="card-index">{{ idx + 1 }}</span>
          <span class="reachability" :class="{ up: server.reachable, down: !server.reachable }">
            {{ server.reachable ? 'reachable' : 'unreachable' }}
          </span>
          <button class="btn-remove" @click="removeServer(idx)">✕</button>
        </div>

        <div class="field-grid">
          <label class="field">
            <span class="field-label">Name</span>
            <input v-model="server.id" type="text" placeholder="MadMapper" />
          </label>
          <label class="field">
            <span class="field-label">Host / IP</span>
            <input v-model="server.ip" type="text" placeholder="127.0.0.1" />
          </label>
          <label class="field">
            <span class="field-label">Query Port</span>
            <input v-model.number="server.queryPort" type="number" min="1" max="65535" />
          </label>
          <label class="field">
            <span class="field-label">Send Port</span>
            <input v-model.number="server.sendPort" type="number" min="1" max="65535" />
          </label>
          <label class="field">
            <span class="field-label">Feedback Port</span>
            <input v-model.number="server.feedbackPort" type="number" min="1" max="65535" />
          </label>
          <label class="field">
            <span class="field-label">Discovery</span>
            <select v-model="server.discovery">
              <option value="manual">Manual</option>
              <option value="bonjour">Bonjour</option>
            </select>
          </label>
        </div>
      </div>
    </div>

    <div v-if="availableAnnouncements.length" class="bonjour-section">
      <div class="section-header">Bonjour Discovered</div>
      <div class="announcement-list">
        <div v-for="item in availableAnnouncements" :key="item.localKey" class="announcement-row">
          <div class="announcement-info">
            <span class="announcement-id">{{ item.id }}</span>
            <span class="announcement-meta">{{ item.ip }}:{{ item.queryPort }}</span>
          </div>
          <button class="btn-secondary" @click="addAnnounced(item)">Add</button>
        </div>
      </div>
    </div>

    <div class="footer-actions">
      <button class="btn-secondary" @click="addManual">+ Add Endpoint</button>
      <button class="btn-primary" @click="save" :disabled="saving">{{ saving ? 'Saving…' : 'Save' }}</button>
    </div>

    <div v-if="status" class="status-bar" :class="status.type">{{ status.message }}</div>

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
        status.value = { type: 'error', message: 'At least one valid endpoint is required.' }
        return
      }

      saving.value = true
      emit('save-config', payload)
      status.value = { type: 'success', message: 'Saved — reconnecting.' }
      setTimeout(() => { saving.value = false }, 350)
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
* { box-sizing: border-box; }

.endpoint-config {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow: hidden;
}

/* ── Section header — matches PageManager style ── */
.section-header {
  padding: 5px 12px;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: var(--text-dim);
  background: var(--bg-shell);
  border-bottom: 1px solid var(--border-strong);
  flex-shrink: 0;
}

/* ── Endpoint list ── */
.endpoint-list {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 0;
}

.endpoint-card {
  border-bottom: 1px solid var(--border-strong);
  padding: 8px 12px 10px;
  background: var(--bg-panel);
}

.endpoint-card:hover {
  background: var(--bg-hover);
}

/* ── Card header row ── */
.card-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.card-index {
  font-size: 10px;
  font-weight: 700;
  color: var(--text-dim);
  letter-spacing: 0.06em;
  width: 16px;
  text-align: center;
}

.reachability {
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  padding: 1px 6px;
  border: 1px solid transparent;
  border-radius: var(--radius-xs);
}

.reachability.up {
  color: #4caf50;
  border-color: rgba(76, 175, 80, 0.3);
  background: rgba(76, 175, 80, 0.08);
}

.reachability.down {
  color: var(--text-dim);
  border-color: var(--border-soft);
  background: transparent;
}

.btn-remove {
  margin-left: auto;
  background: transparent;
  border: none;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 11px;
  padding: 2px 6px;
  border-radius: var(--radius-xs);
  line-height: 1;
}

.btn-remove:hover {
  color: var(--error-text);
  background: rgba(198, 40, 40, 0.12);
}

/* ── Field grid ── */
.field-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 6px 12px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 3px;
  cursor: default;
}

.field-label {
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.07em;
  text-transform: uppercase;
  color: var(--text-dim);
}

input,
select {
  width: 100%;
  height: 26px;
  padding: 0 7px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: var(--bg-input);
  color: var(--text-main);
  font-size: 11px;
}

input:focus,
select:focus {
  outline: none;
  border-color: var(--accent-dim);
  box-shadow: 0 0 0 2px rgba(24, 200, 218, 0.1);
}

/* ── Bonjour section ── */
.bonjour-section {
  flex-shrink: 0;
  border-top: 1px solid var(--border-strong);
}

.announcement-list {
  display: flex;
  flex-direction: column;
}

.announcement-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 7px 12px;
  border-bottom: 1px solid var(--border-strong);
}

.announcement-row:last-child { border-bottom: none; }

.announcement-info {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.announcement-id {
  font-size: 11px;
  font-weight: 600;
  color: var(--text-main);
}

.announcement-meta {
  font-size: 10px;
  color: var(--text-dim);
}

/* ── Footer ── */
.footer-actions {
  display: flex;
  gap: 6px;
  padding: 8px 12px;
  border-top: 1px solid var(--border-strong);
  background: var(--bg-shell);
  flex-shrink: 0;
}

.btn-secondary {
  height: 28px;
  padding: 0 10px;
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-xs);
  background: var(--bg-panel-soft);
  color: var(--text-muted);
  cursor: pointer;
  font-size: 11px;
  font-weight: 600;
}

.btn-secondary:hover {
  background: var(--bg-hover);
  border-color: var(--border-strong);
}

.btn-primary {
  height: 28px;
  padding: 0 14px;
  border: 1px solid var(--accent-dim);
  border-radius: var(--radius-xs);
  background: var(--bg-active);
  color: var(--accent);
  cursor: pointer;
  font-size: 11px;
  font-weight: 600;
  margin-left: auto;
}

.btn-primary:hover {
  background: rgba(18, 200, 218, 0.2);
}

.btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* ── Status bar ── */
.status-bar {
  padding: 6px 12px;
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.04em;
  flex-shrink: 0;
}

.status-bar.success {
  color: #4caf50;
  background: rgba(76, 175, 80, 0.08);
  border-top: 1px solid rgba(76, 175, 80, 0.2);
}

.status-bar.error {
  color: var(--error-text);
  background: var(--error-bg);
  border-top: 1px solid rgba(198, 40, 40, 0.2);
}

@media (max-width: 560px) {
  .field-grid { grid-template-columns: 1fr; }
}
</style>
