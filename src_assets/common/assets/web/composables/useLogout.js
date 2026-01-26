/**
 * Logout composable: hits /api/logout with explicit wrong credentials via XHR.
 * No redirect: on 401 the browser may show the password dialog once; redirecting
 * would load / and trigger another auth prompt. On 200 (localhost), onLocalhost
 * callback runs so the UI can show a reminder.
 */
export function useLogout() {
  /**
   * @param { { onLocalhost?: () => void } } [opts] - If /api/logout returns 200, onLocalhost() is called.
   */
  const logout = (opts = {}) => {
    const xhr = new XMLHttpRequest()
    xhr.open('GET', '/api/logout', true)
    xhr.setRequestHeader('Authorization', 'Basic ' + btoa('logout:logout'))
    xhr.onreadystatechange = () => {
      if (xhr.readyState !== 4) return
      if (xhr.status === 200) {
        opts.onLocalhost?.()
        return
      }
      // 401 or other: do not redirect to avoid triggering login dialog again on /
    }
    xhr.onerror = () => {}
    xhr.ontimeout = () => {}
    xhr.send()
  }
  return { logout }
}
