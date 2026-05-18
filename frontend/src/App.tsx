import { useState, useEffect, useRef } from 'react'
import { ListingResult, FsItem } from './types/drive'
import { listDir, createDir, deleteDir, uploadFile, downloadFile, deleteFile } from './api/driveApi'

export default function App() {
  const [path, setPath]       = useState('/')
  const [listing, setListing] = useState<ListingResult | null>(null)
  const [error, setError]     = useState<string | null>(null)
  const [loading, setLoading] = useState(false)
  const fileInputRef          = useRef<HTMLInputElement>(null)

  useEffect(() => { load(path) }, [path])

  async function load(p: string) {
    setLoading(true)
    setError(null)
    try {
      setListing(await listDir(p))
    } catch (e: any) {
      setError(e.message)
    } finally {
      setLoading(false)
    }
  }

  function navigate(name: string) {
    setPath(path === '/' ? `/${name}` : `${path}/${name}`)
  }

  function navigateUp() {
    if (path === '/') return
    const parent = path.substring(0, path.lastIndexOf('/')) || '/'
    setPath(parent)
  }

  async function handleCreateDir() {
    const name = prompt('Folder name:')
    if (!name) return
    const newPath = path === '/' ? `/${name}` : `${path}/${name}`
    try {
      await createDir(newPath)
      load(path)
    } catch (e: any) {
      setError(e.message)
    }
  }

  async function handleUpload(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0]
    if (!file) return
    const filePath = path === '/' ? `/${file.name}` : `${path}/${file.name}`
    try {
      await uploadFile(filePath, file)
      load(path)
    } catch (e: any) {
      setError(e.message)
    } finally {
      e.target.value = ''
    }
  }

  async function handleDelete(item: FsItem) {
    if (!confirm(`Delete "${decodeURIComponent(item.name)}"?`)) return
    const itemPath = path === '/' ? `/${item.name}` : `${path}/${item.name}`
    try {
      if (item.type === 'dir') await deleteDir(itemPath)
      else await deleteFile(itemPath)
      load(path)
    } catch (e: any) {
      setError(e.message)
    }
  }

  async function handleDownload(name: string) {
    const filePath = path === '/' ? `/${name}` : `${path}/${name}`
    try {
      await downloadFile(filePath, decodeURIComponent(name))
    } catch (e: any) {
      setError(e.message)
    }
  }

  const segments = path === '/' ? [] : path.split('/').filter(Boolean)

  return (
    <div className="min-h-screen bg-sky-50">
      {/* Header */}
      <header className="bg-sky-600 shadow-md px-6 py-4 flex items-center justify-between">
        <div className="flex items-center gap-3">
          <span className="text-2xl">☁️</span>
          <h1 className="text-xl font-bold text-white tracking-wide">RaftDrive</h1>
        </div>
        <div className="flex gap-2">
          <button
            onClick={handleCreateDir}
            className="flex items-center gap-1.5 px-4 py-2 text-sm font-medium bg-white text-sky-700 rounded-lg hover:bg-sky-50 transition"
          >
            <span>📁</span> New Folder
          </button>
          <button
            onClick={() => fileInputRef.current?.click()}
            className="flex items-center gap-1.5 px-4 py-2 text-sm font-medium bg-sky-500 text-white rounded-lg hover:bg-sky-400 transition border border-sky-400"
          >
            <span>⬆️</span> Upload
          </button>
          <input ref={fileInputRef} type="file" className="hidden" onChange={handleUpload} />
        </div>
      </header>

      <main className="max-w-4xl mx-auto px-6 py-8">
        {/* Breadcrumb */}
        <nav className="flex items-center gap-1 text-sm mb-6 bg-white border border-sky-100 rounded-xl px-4 py-3 shadow-sm">
          <button
            onClick={() => setPath('/')}
            className="flex items-center gap-1 text-sky-600 hover:text-sky-800 font-medium"
          >
            <span>🏠</span> Home
          </button>
          {segments.map((seg, i) => {
            const crumbPath = '/' + segments.slice(0, i + 1).join('/')
            const isLast = i === segments.length - 1
            return (
              <span key={i} className="flex items-center gap-1">
                <span className="text-sky-300">/</span>
                {isLast ? (
                  <span className="text-gray-700 font-medium">{decodeURIComponent(seg)}</span>
                ) : (
                  <button
                    onClick={() => setPath(crumbPath)}
                    className="text-sky-600 hover:text-sky-800"
                  >
                    {decodeURIComponent(seg)}
                  </button>
                )}
              </span>
            )
          })}
        </nav>

        {/* Error */}
        {error && (
          <div className="mb-4 px-4 py-3 bg-red-50 border border-red-200 text-red-700 rounded-xl text-sm">
            {error}
          </div>
        )}

        {/* File listing */}
        <div className="bg-white border border-sky-100 rounded-2xl shadow-sm overflow-hidden">
          {/* Table header */}
          <div className="grid grid-cols-12 px-4 py-2 bg-sky-50 border-b border-sky-100 text-xs font-semibold text-sky-600 uppercase tracking-wide">
            <span className="col-span-6">Name</span>
            <span className="col-span-2 text-right">Size</span>
            <span className="col-span-4 text-right">Actions</span>
          </div>

          {/* Go up row */}
          {path !== '/' && (
            <button
              onClick={navigateUp}
              className="w-full grid grid-cols-12 items-center px-4 py-3 hover:bg-sky-50 border-b border-gray-100 text-sm text-gray-500 transition"
            >
              <span className="col-span-6 flex items-center gap-3">
                <span>⬆️</span>
                <span className="italic">..</span>
              </span>
            </button>
          )}

          {loading && (
            <div className="px-4 py-12 text-center text-sm text-sky-400">
              <div className="animate-pulse">Loading...</div>
            </div>
          )}

          {!loading && listing?.children.length === 0 && (
            <div className="px-4 py-12 text-center text-sm text-gray-400">
              <div className="text-4xl mb-2">📭</div>
              <div>This folder is empty</div>
            </div>
          )}

          {!loading && listing?.children.map((item, i) => (
            <div
              key={i}
              className="grid grid-cols-12 items-center px-4 py-3 hover:bg-sky-50 border-b border-gray-100 last:border-b-0 transition"
            >
              {/* Icon + Name */}
              <span
                className={`col-span-6 flex items-center gap-3 text-sm ${
                  item.type === 'dir' ? 'text-sky-600 cursor-pointer hover:underline font-medium' : 'text-gray-800'
                }`}
                onClick={() => item.type === 'dir' && navigate(item.name)}
              >
                <span className="text-xl">{item.type === 'dir' ? '📁' : '📄'}</span>
                {decodeURIComponent(item.name)}
              </span>

              {/* Size */}
              <span className="col-span-2 text-xs text-gray-400 text-right">
                {item.type === 'file'
                  ? item.size < 1024
                    ? `${item.size} B`
                    : `${(item.size / 1024).toFixed(1)} KB`
                  : '—'}
              </span>

              {/* Actions */}
              <div className="col-span-4 flex justify-end gap-2">
                {item.type === 'file' && (
                  <button
                    onClick={() => handleDownload(item.name)}
                    className="text-xs px-3 py-1 text-sky-600 bg-sky-50 hover:bg-sky-100 rounded-lg border border-sky-200 transition"
                  >
                    Download
                  </button>
                )}
                <button
                  onClick={() => handleDelete(item)}
                  className="text-xs px-3 py-1 text-red-500 bg-red-50 hover:bg-red-100 rounded-lg border border-red-200 transition"
                >
                  Delete
                </button>
              </div>
            </div>
          ))}
        </div>
      </main>
    </div>
  )
}
