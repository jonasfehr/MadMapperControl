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

  // Fetch OSCQuery parameter tree from all servers
  async fetchParameters() {
    const response = await axios.get(`${API_BASE}/parameters`)
    return response.data
  },

  // Fetch current app configuration
  async fetchConfig() {
    const response = await axios.get(`${API_BASE}/config`)
    return response.data
  }
}
