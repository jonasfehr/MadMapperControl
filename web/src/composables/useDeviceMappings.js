import { computed } from 'vue'

function wildcardToRegExp(pattern) {
  const escaped = pattern.replace(/[.+?^${}()|[\]\\]/g, '\\$&')
  return new RegExp('^' + escaped.replace(/\*/g, '.*') + '$', 'i')
}

function normalizeElement(el, defaultServerId = 0) {
  if (el && typeof el === 'object') return { path: el.path || '', serverId: el.serverId ?? defaultServerId }
  return { path: String(el), serverId: defaultServerId }
}

function serverIdNum(str) {
  return Number(String(str).replace('server_', '')) || 0
}

function extractValue(param) {
  if (!param) return null
  const v = param.value
  if (Array.isArray(v)) return v[0] ?? null
  return v ?? null
}

export function useDeviceMappings(pagesRef, configRef, allParametersRef) {
  const currentPageName = computed(() => configRef.value?.currentPage || '')

  const currentPage = computed(() => {
    const ps = pagesRef.value?.pages || []
    return ps.find(p => p.name === currentPageName.value) || null
  })

  const resolvedChannels = computed(() => {
    const page = currentPage.value
    if (!page) return []
    const elements = Array.isArray(page.elements) ? page.elements : []
    const allParams = allParametersRef.value || []
    const result = []

    for (const rawEl of elements) {
      const el = normalizeElement(rawEl, page.serverId || 0)
      const { path, serverId: srvId } = el
      if (!path) continue

      // Normalize to number for comparison — srvId may be 0, 'server_0', etc.
      const srvIdNum = serverIdNum(srvId)

      if (!path.includes('*')) {
        const found = allParams.find(p => serverIdNum(p.serverId) === srvIdNum && p.path === path)
        result.push({ path, serverId: srvId, label: shortLabel(path), value: extractValue(found) })
      } else {
        const re = wildcardToRegExp(path)
        const matches = allParams.filter(p => serverIdNum(p.serverId) === srvIdNum && re.test(p.path))
        for (const m of matches) result.push({ path: m.path, serverId: srvId, label: shortLabel(m.path), value: extractValue(m) })
      }
    }
    return result
  })

  return { currentPageName, currentPage, resolvedChannels }
}

function shortLabel(path) {
  const parts = path.split('/').filter(Boolean)
  if (parts.length >= 2) return parts[parts.length - 2]
  return parts[0] || path
}
