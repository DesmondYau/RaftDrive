import { ListingResult } from '../types/drive'

const base = import.meta.env.VITE_API_BASE ?? '/api'

export async function listDir(path: string): Promise<ListingResult> {
  const url = path === '/' ? `${base}/dirs/` : `${base}/dirs${path}`
  const res = await fetch(url)
  if (!res.ok) throw new Error(`listDir failed: ${res.status}`)
  return res.json()
}

export async function createDir(path: string): Promise<void> {
  const url = path === '/' ? `${base}/dirs/` : `${base}/dirs${path}`
  const res = await fetch(url, { method: 'POST' })
  if (!res.ok) throw new Error(`createDir failed: ${res.status}`)
}

export async function deleteDir(path: string): Promise<void> {
  const url = path === '/' ? `${base}/dirs/` : `${base}/dirs${path}`
  const res = await fetch(url, { method: 'DELETE' })
  if (!res.ok) throw new Error(`deleteDir failed: ${res.status}`)
}

export async function uploadFile(path: string, file: File): Promise<void> {
  const url = `${base}/files${path}`
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': file.type || 'application/octet-stream' },
    body: file,
  })
  if (!res.ok) throw new Error(`uploadFile failed: ${res.status}`)
}

export async function downloadFile(path: string, filename: string): Promise<void> {
  const url = `${base}/files${path}`
  const res = await fetch(url)
  if (!res.ok) throw new Error(`downloadFile failed: ${res.status}`)
  const blob = await res.blob()
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = filename
  a.click()
  URL.revokeObjectURL(a.href)
}

export async function deleteFile(path: string): Promise<void> {
  const url = `${base}/files${path}`
  const res = await fetch(url, { method: 'DELETE' })
  if (!res.ok) throw new Error(`deleteFile failed: ${res.status}`)
}
