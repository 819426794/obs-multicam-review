// ============================================================
// REST API 客户端 — obs-multicam-review
// 封装所有后端 REST 端点调用，类型安全
// ============================================================

import type * as T from '../types/api';

const BASE = '/api';

// ============ 通用请求封装 ============

class ApiError extends Error {
  constructor(
    public code: string,
    message: string,
    public status: number,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  const init: RequestInit = { method, headers };
  if (body !== undefined) init.body = JSON.stringify(body);

  let res: Response;
  try {
    res = await fetch(`${BASE}${path}`, init);
  } catch (err) {
    throw new Error(`Network error: ${err instanceof Error ? err.message : String(err)}`);
  }

  if (!res.ok) {
    let error: T.ApiError | undefined;
    try { error = await res.json(); } catch { /* no body */ }
    throw new ApiError(
      error?.code ?? 'ERR_INTERNAL',
      error?.message ?? `HTTP ${res.status}`,
      res.status,
    );
  }

  return res.json();
}

// ============ 系统 ============

export function fetchHealth() {
  return request<T.HealthResponse>('GET', '/system/health');
}

export function fetchSystemStatus() {
  return request<T.SystemStatus>('GET', '/system/status');
}

// ============ 音频 ============

export function fetchAudioChannels() {
  return request<{ channels: T.AudioChannel[] }>('GET', '/audio/channels');
}

export function setVolume(sourceName: string, volume: number) {
  return request<void>('POST', '/audio/volume', { sourceName, volume });
}

export function setMute(sourceName: string, muted: boolean) {
  return request<void>('POST', '/audio/mute', { sourceName, muted });
}

export function setSolo(sourceName: string, solo: boolean) {
  return request<void>('POST', '/audio/solo', { sourceName, solo });
}

export function setPan(sourceName: string, pan: number) {
  return request<void>('POST', '/audio/pan', { sourceName, pan });
}

export function fetchMasterVolume() {
  return request<{ volume: number }>('GET', '/audio/master-volume');
}

export function setMasterVolume(volume: number) {
  return request<void>('POST', '/audio/master-volume', { volume });
}

// ============ 录制 ============

export function recStart(body?: { outputDir: string }) {
  return request<T.RecStartResponse>('POST', '/rec/start', body);
}

export function recStop() {
  return request<T.RecStopResponse>('POST', '/rec/stop');
}

export function recPause() {
  return request<void>('POST', '/rec/pause');
}

export function recResume() {
  return request<void>('POST', '/rec/resume');
}

export function fetchRecordingStatus() {
  return request<T.RecStatus>('GET', '/rec/status');
}

// ============ 场景 ============

export function fetchSceneList() {
  return request<T.SceneListResponse>('GET', '/scene/list');
}

export function switchScene(body: T.SceneSwitchRequest) {
  return request<void>('POST', '/scene/switch', body);
}

export function createScene(body: T.SceneCreateRequest) {
  return request<T.SceneListItem>('POST', '/scene/create', body);
}

export function deleteScene(sceneName: string) {
  return request<void>('DELETE', `/scene/${encodeURIComponent(sceneName)}`);
}

// ============ 输入源 ============

export function fetchSourceList() {
  return request<T.SourceListResponse>('GET', '/source/list');
}

export function discoverSources() {
  return request<T.SourceListResponse>('POST', '/source/discover');
}

export function renameSource(body: T.SourceRenameRequest) {
  return request<void>('POST', '/source/rename', body);
}

export function showSource(body: T.SourceShowHideRequest) {
  return request<void>('POST', '/source/show', body);
}

export function hideSource(body: T.SourceShowHideRequest) {
  return request<void>('POST', '/source/hide', body);
}

export function configureSource(body: T.SourceConfigureRequest) {
  return request<void>('POST', '/source/configure', body);
}

// ============ 标记 ============

export function fetchMarkerList(recordingId: string) {
  return request<T.Marker[]>(
    'GET', `/marker/list?${new URLSearchParams({ recordingId })}`,
  );
}

export function addMarker(body: T.MarkerAddRequest) {
  return request<T.Marker>('POST', '/marker/add', body);
}

// ============ 时间码 ============

export function fetchTimecode() {
  return request<{ smpte: string; seconds: number; frameNumber: number }>('GET', '/timecode');
}

// ============ 叠加层 ============

export function fetchOverlayConfig() {
  return request<Record<string, unknown>>('GET', '/overlay/config');
}

export function saveOverlayConfig(body: Record<string, unknown>) {
  return request<void>('POST', '/overlay/config', body);
}

// ============ 预设 ============

export function fetchPresetList() {
  return request<T.PresetListItem[]>('GET', '/preset/list');
}

export function savePreset(name: string) {
  return request<void>('POST', '/preset/save', { name });
}

export function loadPreset(name: string) {
  return request<void>('POST', '/preset/load', { name });
}

export function deletePreset(name: string) {
  return request<void>('POST', '/preset/delete', { name });
}

// ============ 设置 ============

export function fetchSettings() {
  return request<Record<string, unknown>>('GET', '/settings');
}

export function saveSettings(body: Record<string, unknown>) {
  return request<void>('POST', '/settings', body);
}

// ============ 项目 ============

export function fetchProjectList() {
  return request<T.ProjectListResponse>('GET', '/projects');
}

export function createProject(body: T.ProjectCreateRequest) {
  return request<T.Project>('POST', '/projects', body);
}

export function updateProject(id: string, body: T.ProjectUpdateRequest) {
  return request<T.Project>('PUT', `/projects/${id}`, body);
}

export function deleteProject(id: string) {
  return request<void>('DELETE', `/projects/${id}`);
}

// ============ 产品 ============

export function fetchProductList(projectId: string) {
  return request<T.ProductListResponse>(
    'GET', `/products?${new URLSearchParams({ projectId })}`,
  );
}

export function createProduct(body: T.ProductCreateRequest) {
  return request<T.Product>('POST', '/products', body);
}

export function updateProduct(id: string, body: T.ProductUpdateRequest) {
  return request<T.Product>('PUT', `/products/${id}`, body);
}

export function deleteProduct(id: string) {
  return request<void>('DELETE', `/products/${id}`);
}

export function reorderProducts(body: T.ProductReorderRequest) {
  return request<void>('PUT', '/products/reorder', body);
}

// ============ 评分维度 ============

export function fetchDimTemplates() {
  return request<T.DimensionTemplate[]>('GET', '/dimensions/templates');
}

export function createDimTemplate(body: T.DimensionTemplateCreateRequest) {
  return request<T.DimensionTemplate>('POST', '/dimensions/templates', body);
}

export function deleteDimTemplate(id: string) {
  return request<void>('DELETE', `/dimensions/templates/${id}`);
}

export function bindDimTemplate(productId: string, body: T.BindDimTemplateRequest) {
  return request<void>('PUT', `/products/${productId}/dimension-template`, body);
}

export function fetchProductDimTemplate(productId: string) {
  return request<T.DimensionTemplate>(
    'GET', `/products/${productId}/dimension-template`,
  );
}

// ============ 评分会话 ============

export function fetchScoringSessions(projectId?: string) {
  const qs = projectId
    ? `?${new URLSearchParams({ projectId })}`
    : '';
  return request<{ sessions: T.ScoringSession[] }>('GET', `/scoring/sessions${qs}`);
}

export function createScoringSession(body: T.ScoringSessionCreateRequest) {
  return request<T.ScoringSession>('POST', '/scoring/sessions', body);
}

export function fetchScoringSession(id: string) {
  return request<T.ScoringSession>('GET', `/scoring/sessions/${id}`);
}

export function completeScoringSession(id: string) {
  return request<T.ScoringSession>('POST', `/scoring/sessions/${id}/complete`);
}

export function submitScore(body: T.ScoreSubmitRequest) {
  return request<T.ScoreEntry>('POST', '/scoring/scores', body);
}

export function fetchScores(sessionId: string, productId: string) {
  return request<T.ScoreEntry[]>(
    'GET', `/scoring/scores?${new URLSearchParams({ sessionId, productId })}`,
  );
}

export function fetchLeaderboard(sessionId: string) {
  return request<T.LeaderboardResponse>(
    'GET', `/scoring/leaderboard?${new URLSearchParams({ sessionId })}`,
  );
}
