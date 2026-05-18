export interface FileMeta {
  type: 'file'
  name: string
  size: number
  content_type: string
}

export interface DirMeta {
  type: 'dir'
  name: string
}

export type FsItem = FileMeta | DirMeta

export interface ListingResult {
  path: string
  name: string
  children: FsItem[]
}
