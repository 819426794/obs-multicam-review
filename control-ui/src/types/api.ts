// ============================================================
// TypeScript 类型定义 — obs-multicam-review
// 与 docs/API_SPEC.md 保持同步，是前后端接口的唯一类型源
// ============================================================

// ============ 通用信封 ============
export interface MessageEnvelope<T = unknown> {
  type: 'request' | 'response' | 'event';
  id: string;                              // UUID v4
  action: string;
  payload: T;
  timestamp: string;                       // ISO 8601 + TZ
  error?: ApiError;
}

export interface ApiError {
  code: string;
  message: string;
}

// ============ 录制 ============
export interface RecStartResponse {
  recordingId: string;
  startTime: string;
  outputDir: string;
}

export interface RecStopResponse {
  recordingId: string;
  duration: number;          // 秒
  files: RecFile[];
}

export interface RecFile {
  sourceId: string;
  path: string;
  size: number;              // 字节
}

export type RecordingState = 'idle' | 'recording' | 'paused';

// ============ 场景 ============
export interface SceneListItem {
  name: string;
  sources: string[];
}

export interface SceneListResponse {
  currentScene: string;
  scenes: SceneListItem[];
}

export interface SceneSwitchRequest {
  sceneName: string;
  transitionId?: string;     // 可选，默认 cut
}

// ============ 输入源 ============
export interface SourceInfo {
  id: string;
  name: string;
  type: string;
  active: boolean;
  resolution: { width: number; height: number };
  fps: number;
  audioLevel: number;        // dB
}

export interface SourceListResponse {
  sources: SourceInfo[];
}

export interface SourceShowHideRequest {
  sourceName: string;
}

export interface SourceConfigureRequest {
  sourceName: string;
  properties: Record<string, unknown>;
}

// ============ 标记 ============
export interface Marker {
  id: string;
  name: string;
  type: string;              // 'manual' | 'auto' | 'scoring'
  timecode: string;          // HH:MM:SS:FF
  timestamp: string;
  metadata?: Record<string, unknown>;
}

export interface MarkerAddRequest {
  name: string;
  type: string;
  metadata?: Record<string, unknown>;
}

// ============ 系统 ============
export interface SystemStatus {
  pluginVersion: string;
  obsVersion: string;
  recordingState: RecordingState;
  currentScene: string;
  timecode: string;
  fps: number;
  diskFreeBytes: number;
  cpuUsage: number;
  memoryUsageMB: number;
}

export interface HealthResponse {
  status: string;
  uptime: number;
}

// ============ 项目 ============
export interface Project {
  id: string;
  name: string;
  status: 'draft' | 'recording' | 'completed';
  fps: number;
  resolutionX: number;
  resolutionY: number;
  brandMaskEnabled: boolean;
  brandMaskRule: string;
  brandMaskChar: string;
  createdAt: string;
  updatedAt: string;
}

export interface ProjectListResponse {
  projects: Project[];
}

export interface ProjectCreateRequest {
  name: string;
  fps: number;
  resolutionX: number;
  resolutionY: number;
}

export interface ProjectUpdateRequest {
  name?: string;
  fps?: number;
  resolutionX?: number;
  resolutionY?: number;
  brandMaskEnabled?: boolean;
  brandMaskRule?: string;
  brandMaskChar?: string;
}

// ============ 产品 ============
export interface Product {
  id: string;
  projectId: string;
  sortOrder: number;
  brand: string;
  model: string;
  price: string;
  priceValue: number;
  color: string;
  colorHex: string;
  spec: string;
  specJson: string;
  imagePath: string;
  notes: string;
  createdAt: string;
}

export interface ProductListResponse {
  products: Product[];
}

export interface ProductReorderRequest {
  projectId: string;
  ids: string[];
}

export interface ProductCreateRequest {
  projectId: string;
  brand: string;
  model: string;
  price?: string;
  priceValue?: number;
  color?: string;
  colorHex?: string;
  specJson?: string;
  imagePath?: string;
  notes?: string;
}

export interface ProductUpdateRequest {
  brand?: string;
  model?: string;
  price?: string;
  priceValue?: number;
  color?: string;
  colorHex?: string;
  specJson?: string;
  imagePath?: string;
  notes?: string;
}

// ============ 评分维度 ============
export interface DimensionTemplate {
  id: string;
  name: string;
  isBuiltin: boolean;
  createdAt: string;
}

export interface DimensionItem {
  id?: string;
  dimKey: string;
  label: string;
  weight: number;
  maxScore: number;
  sortOrder: number;
}

export interface DimensionTemplateCreateRequest {
  name: string;
  items: DimensionItem[];
}

export interface BindDimTemplateRequest {
  templateId: string;
}

// ============ 评分会话 ============
export interface ScoringSession {
  id: string;
  projectId: string;
  recordingId?: string;
  judgeName: string;
  sessionName: string;
  startedAt: string;
  completedAt?: string;
  status: 'in_progress' | 'completed';
}

export interface ScoringSessionCreateRequest {
  projectId: string;
  judgeName: string;
  sessionName: string;
}

export interface ScoreEntry {
  id: string;
  sessionId: string;
  productId: string;
  dimKey: string;
  score: number;
  maxScore: number;
  note: string;
  createdAt: string;
}

export interface ScoreSubmitRequest {
  sessionId: string;
  productId: string;
  dimKey: string;
  score: number;
  note?: string;
}

export interface LeaderboardEntry {
  productId: string;
  brand: string;
  model: string;
  totalScore: number;
  maxPossible: number;
  dimensions: Array<{
    dimKey: string;
    label: string;
    score: number;
    maxScore: number;
  }>;
}

export interface LeaderboardResponse {
  leaderboard: LeaderboardEntry[];
}

// ============ 预设 ============
export interface PresetSaveRequest {
  presetName: string;
  overwrite?: boolean;
}

export interface PresetLoadRequest {
  presetId: string;
}

// ============ WebSocket 事件 ============
export type WsEventAction =
  | 'recording.started'
  | 'recording.stopped'
  | 'recording.paused'
  | 'recording.resumed'
  | 'timecode.tick'
  | 'source.status'
  | 'source.added'
  | 'source.removed'
  | 'marker.added'
  | 'scene.changed'
  | 'audio.level'
  | 'system.status'
  | 'system.heartbeat'
  | 'system.error';

export type WsCommandAction =
  | 'rec.start'
  | 'rec.stop'
  | 'rec.pause'
  | 'rec.resume'
  | 'scene.switch'
  | 'source.show'
  | 'source.hide'
  | 'marker.add'
  | 'sfx.play'
  | 'preset.load'
  | 'system.pong';

// ============ API 响应联合类型（用于 api-client 泛型推断） ============
export type ApiResponse<T> = T;
