import axios from 'axios'

const API_BASE = '/api'

export const apiClient = {
  // Fetch all pages from custom_page.json
  async fetchPages() {
    const response = await axios.get(`${API_BASE}/pages`)
    return response.data
  },

  // Save pages to custom_page.json
  async savePages(pages) {
    const response = await axios.post(`${API_BASE}/pages`, pages)
    return response.data
  },

  // Activate a page on the controller without editing the file
  async activatePage(pageName) {
    const response = await axios.post(`${API_BASE}/pages`, { currentPage: pageName })
    return response.data
  },

  // Fetch OSCQuery parameter tree from all servers
  async fetchParameters() {
    const response = await axios.get(`${API_BASE}/parameters`)
    return response.data
  },

  // Fetch current app configuration
  async fetchConfig() {
    const response = await axios.get(`${API_BASE}/config`)
    return response.data
  },

  // Save endpoint/server configuration
  async saveConfig(config) {
    const response = await axios.post(`${API_BASE}/config`, config)
    return response.data
  },

  // Mappings (nav + fixed OSC targets)
  async fetchMappings() {
    const response = await axios.get(`${API_BASE}/mappings`)
    return response.data
  },
  async saveMappings(mappings) {
    const response = await axios.post(`${API_BASE}/mappings`, mappings)
    return response.data
  },

  // Active device profile (components + bindings)
  async fetchProfiles() {
    const response = await axios.get(`${API_BASE}/profiles`)
    return response.data
  },
  async fetchProfile() {
    const response = await axios.get(`${API_BASE}/profile`)
    return response.data
  },
  async saveProfile(profile) {
    const response = await axios.post(`${API_BASE}/profile`, profile)
    return response.data
  },

  // MIDI Learn
  async learnStart() {
    const response = await axios.post(`${API_BASE}/learn/start`)
    return response.data
  },
  async learnStop() {
    const response = await axios.post(`${API_BASE}/learn/stop`)
    return response.data
  },
  async learnStatus() {
    const response = await axios.get(`${API_BASE}/learn/status`)
    return response.data
  },
  async learnAssign(payload) {
    const response = await axios.post(`${API_BASE}/learn/assign`, payload)
    return response.data
  },

  async learnInject(payload) {
    const response = await axios.post(`${API_BASE}/learn/inject`, payload)
    return response.data
  },

  // Emulator bridge: inject raw MIDI into the active surface (same path as hardware)
  async injectMidi(payload) {
    const response = await axios.post(`${API_BASE}/midi`, payload)
    return response.data
  },

  // Live device display state (page, parameter labels/values, cue grid)
  async fetchDisplay() {
    const response = await axios.get(`${API_BASE}/display`)
    return response.data
  }
}
