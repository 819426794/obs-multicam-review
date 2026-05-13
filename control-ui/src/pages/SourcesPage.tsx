// ============================================================
// 输入源管理页 — obs-multicam-review
// ============================================================
import { useState } from 'react';
import {
  Camera, RefreshCw, Loader2, AlertTriangle,
  Monitor, Eye, EyeOff, SlidersHorizontal,
} from 'lucide-react';
import { useApi } from '../hooks/useApi';
import * as api from '../services/api-client';
import { Modal } from '../components/Modal';
import type { SourceInfo, SourceCategory } from '../types/api';

// ── 常量 ──────────────────────────────────────────────

const CATEGORY_ICONS: Record<SourceCategory, { icon: string; label: string }> = {
  camera:  { icon: '📷', label: 'Camera' },
  desktop: { icon: '🖥️', label: 'Desktop' },
  window:  { icon: '🪟', label: 'Window' },
  browser: { icon: '🌐', label: 'Browser' },
  media:   { icon: '🎬', label: 'Media' },
  audio:   { icon: '🎤', label: 'Audio' },
  ndi:     { icon: '🔗', label: 'NDI' },
  unknown: { icon: '❓', label: 'Other' },
};

// 分组显示顺序
const CATEGORY_ORDER: SourceCategory[] = [
  'camera', 'desktop', 'audio', 'window', 'browser', 'media', 'ndi', 'unknown',
];

const COLOR_PRESETS = [
  '#ef4444', '#f97316', '#eab308', '#22c55e',
  '#3b82f6', '#8b5cf6', '#ec4899', '#ffffff',
];

// ── 工具 ──────────────────────────────────────────────

function groupByCategory(sources: SourceInfo[]): Map<SourceCategory, SourceInfo[]> {
  const map = new Map<SourceCategory, SourceInfo[]>();
  for (const s of sources) {
    const cat = s.category;
    if (!map.has(cat)) map.set(cat, []);
    map.get(cat)!.push(s);
  }
  return map;
}

// ── 音频电平指示条 ────────────────────────────────────

function AudioLevelBar({ level, muted }: { level: number; muted: boolean }) {
  const pct = Math.min(100, Math.max(0, Math.round(level * 100)));

  let gradient: string;
  if (muted || pct === 0) {
    gradient = 'from-slate-600 to-slate-600';
  } else if (pct > 80) {
    gradient = 'from-red-400 to-red-500';
  } else if (pct > 50) {
    gradient = 'from-yellow-400 to-orange-400';
  } else {
    gradient = 'from-green-400 to-emerald-400';
  }

  return (
    <div className="w-12 h-1.5 bg-slate-700/60 rounded-full overflow-hidden">
      <div
        className={`h-full bg-gradient-to-r ${gradient} rounded-full transition-all duration-300 ease-out`}
        style={{ width: muted ? '0%' : `${pct}%` }}
      />
    </div>
  );
}

// ── 单个源卡片 ────────────────────────────────────────

function SourceCard({
  source,
  onToggle,
  onEdit,
  toggling,
}: {
  source: SourceInfo;
  onToggle: (s: SourceInfo) => void;
  onEdit: (s: SourceInfo) => void;
  toggling: boolean;
}) {
  const { icon } = CATEGORY_ICONS[source.category] ?? CATEGORY_ICONS.unknown;
  const isAudioSource = source.category === 'audio';

  return (
    <div
      onClick={() => onEdit(source)}
      className="group flex items-center gap-3 bg-slate-800/60 backdrop-blur-sm
                 border border-slate-700/60 rounded-lg px-3 py-2.5
                 hover:border-slate-600 hover:bg-slate-800/80 cursor-pointer
                 transition-all duration-150"
    >
      {/* 左侧：图标 + 颜色圆点 */}
      <div className="flex items-center gap-2 shrink-0">
        <span className="text-lg leading-none">{icon}</span>
        <span
          className="w-2.5 h-2.5 rounded-full shrink-0"
          style={{ backgroundColor: source.colorTag || '#64748b' }}
        />
      </div>

      {/* 中间：源信息 */}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2">
          <span className="text-sm font-medium text-slate-200 truncate">
            {source.alias || source.obsName}
          </span>
          {source.alias && source.alias !== source.obsName && (
            <span className="text-xs text-slate-500 truncate hidden sm:inline">
              {source.obsName}
            </span>
          )}
        </div>
        <div className="flex items-center gap-2 mt-0.5">
          <span className="text-[11px] text-slate-500 bg-slate-700/50 rounded px-1.5 py-px font-mono">
            {source.type}
          </span>
          {source.resolution.width > 0 && (
            <span className="text-[11px] text-slate-600">
              {source.resolution.width}×{source.resolution.height}
            </span>
          )}
          {source.fps > 0 && (
            <span className="text-[11px] text-slate-600">{source.fps} fps</span>
          )}
        </div>
      </div>

      {/* 右侧：控件区 */}
      <div className="flex items-center gap-3 shrink-0" onClick={e => e.stopPropagation()}>
        {isAudioSource && <AudioLevelBar level={source.audioLevel} muted={source.muted} />}

        {/* 显示/隐藏开关 */}
        <button
          onClick={() => onToggle(source)}
          disabled={toggling}
          className={`p-1.5 rounded-lg transition-all duration-150 ${
            toggling ? 'opacity-50' : ''
          } ${
            source.showing
              ? 'text-green-400 hover:text-green-300 hover:bg-green-400/10'
              : 'text-slate-500 hover:text-slate-300 hover:bg-slate-700'
          }`}
          title={source.showing ? '隐藏' : '显示'}
        >
          {toggling
            ? <Loader2 size={15} className="animate-spin" />
            : source.showing
              ? <Eye size={15} />
              : <EyeOff size={15} />
          }
        </button>

        {/* 编辑提示 */}
        <SlidersHorizontal
          size={13}
          className="text-slate-600 group-hover:text-slate-400 transition-colors"
        />
      </div>
    </div>
  );
}

// ── 骨架屏 ────────────────────────────────────────────

function Skeleton() {
  return (
    <div className="flex items-center justify-center h-full">
      <Loader2 size={32} className="animate-spin text-slate-500" />
    </div>
  );
}

// ── 错误态 ────────────────────────────────────────────

function ErrorState({ error, onRetry }: { error: string; onRetry: () => void }) {
  return (
    <div className="flex flex-col items-center justify-center h-full gap-3 text-slate-400">
      <AlertTriangle size={36} className="text-red-400" />
      <p className="text-sm">{error}</p>
      <button
        onClick={onRetry}
        className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm rounded-lg transition-colors"
      >
        重试
      </button>
    </div>
  );
}

// ── 空状态 ────────────────────────────────────────────

function EmptyState({ onDiscover }: { onDiscover: () => void }) {
  return (
    <div className="flex flex-col items-center justify-center py-16 text-slate-500 gap-4">
      <Monitor size={48} className="opacity-30" />
      <div className="text-center">
        <p className="text-sm font-medium text-slate-400 mb-1">未检测到输入源</p>
        <p className="text-xs text-slate-600">请先打开 OBS 并添加视频源</p>
      </div>
      <button
        onClick={onDiscover}
        className="mt-2 px-4 py-2 bg-slate-800 hover:bg-slate-700 text-slate-300 text-sm
                   rounded-lg border border-slate-700 transition-colors"
      >
        🔍 扫描源
      </button>
    </div>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function SourcesPage() {
  const { data, loading, error, refresh } = useApi(api.fetchSourceList);
  const [discovering, setDiscovering] = useState(false);
  const [editSource, setEditSource] = useState<SourceInfo | null>(null);
  const [editAlias, setEditAlias] = useState('');
  const [editColor, setEditColor] = useState('');
  const [toggling, setToggling] = useState<Set<string>>(new Set());

  const sources = data?.sources ?? [];
  const grouped = groupByCategory(sources);

  // ── 扫描源 ──
  const handleDiscover = async () => {
    setDiscovering(true);
    try {
      await api.discoverSources();
      refresh();
    } catch (err) {
      alert('扫描失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setDiscovering(false);
    }
  };

  // ── 切换显示/隐藏 ──
  const handleToggle = async (source: SourceInfo) => {
    setToggling(prev => new Set(prev).add(source.obsName));
    try {
      if (source.showing) {
        await api.hideSource({ obsName: source.obsName });
      } else {
        await api.showSource({ obsName: source.obsName });
      }
      refresh();
    } catch (err) {
      alert('操作失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setToggling(prev => {
        const next = new Set(prev);
        next.delete(source.obsName);
        return next;
      });
    }
  };

  // ── 打开编辑 ──
  const handleEditOpen = (source: SourceInfo) => {
    setEditSource(source);
    setEditAlias(source.alias);
    setEditColor(source.colorTag || COLOR_PRESETS[0]);
  };

  // ── 保存编辑 ──
  const handleEditSave = async () => {
    if (!editSource) return;
    try {
      await api.configureSource({
        obsName: editSource.obsName,
        alias: editAlias,
        colorTag: editColor,
      });
      setEditSource(null);
      refresh();
    } catch (err) {
      alert('保存失败: ' + (err instanceof Error ? err.message : String(err)));
    }
  };

  // ── 渲染 ──
  if (loading) return <Skeleton />;
  if (error) return <ErrorState error={error} onRetry={refresh} />;

  return (
    <div className="max-w-4xl mx-auto space-y-4">
      {/* 页头 */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <Camera size={20} className="text-blue-400" />
          输入源
        </h2>
        <button
          onClick={handleDiscover}
          disabled={discovering}
          className="flex items-center gap-1.5 px-4 py-2 bg-slate-800 hover:bg-slate-700
                     disabled:bg-slate-800/50 text-slate-300 text-sm font-medium rounded-lg
                     border border-slate-700 transition-all duration-150
                     disabled:cursor-not-allowed"
        >
          {discovering ? (
            <Loader2 size={14} className="animate-spin" />
          ) : (
            <RefreshCw size={14} />
          )}
          {discovering ? '扫描中...' : '🔍 重新扫描'}
        </button>
      </div>

      {/* 空 / 列表 */}
      {sources.length === 0 ? (
        <EmptyState onDiscover={handleDiscover} />
      ) : (
        <div className="space-y-6">
          {CATEGORY_ORDER.map(cat => {
            const items = grouped.get(cat);
            if (!items || items.length === 0) return null;
            const { icon, label } = CATEGORY_ICONS[cat];

            return (
              <div key={cat}>
                {/* 分组标题 */}
                <div className="flex items-center gap-2 mb-2 px-1">
                  <span className="text-sm">{icon}</span>
                  <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">
                    {label}
                  </span>
                  <span className="text-xs text-slate-600">({items.length})</span>
                  <div className="flex-1 border-t border-slate-700/40 ml-2" />
                </div>

                {/* 卡片列表 */}
                <div className="space-y-1.5">
                  {items.map(source => (
                    <SourceCard
                      key={source.id}
                      source={source}
                      onToggle={handleToggle}
                      onEdit={handleEditOpen}
                      toggling={toggling.has(source.obsName)}
                    />
                  ))}
                </div>
              </div>
            );
          })}
        </div>
      )}

      {/* 编辑 Modal */}
      <Modal
        open={editSource !== null}
        onClose={() => setEditSource(null)}
        title="编辑输入源"
        width="sm"
      >
        {editSource && (
          <div className="space-y-4">
            {/* OBS 名称（只读） */}
            <div>
              <label className="block text-xs text-slate-500 uppercase tracking-wide mb-1">
                OBS 名称
              </label>
              <p className="text-sm text-slate-400 bg-slate-800/50 rounded-lg px-3 py-2 font-mono">
                {editSource.obsName}
              </p>
            </div>

            {/* 别名 */}
            <div>
              <label className="block text-xs text-slate-500 uppercase tracking-wide mb-1">
                别名
              </label>
              <input
                value={editAlias}
                onChange={e => setEditAlias(e.target.value)}
                className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2
                           text-slate-200 text-sm focus:outline-none focus:border-blue-500
                           transition-colors"
                placeholder="输入别名…"
                autoFocus
              />
            </div>

            {/* 颜色选择器 */}
            <div>
              <label className="block text-xs text-slate-500 uppercase tracking-wide mb-2">
                颜色标记
              </label>
              <div className="flex gap-2.5 flex-wrap">
                {COLOR_PRESETS.map(color => (
                  <button
                    key={color}
                    onClick={() => setEditColor(color)}
                    className={`w-8 h-8 rounded-full transition-all duration-150 ${
                      editColor === color
                        ? 'ring-2 ring-white ring-offset-2 ring-offset-slate-900 scale-110'
                        : 'hover:scale-110'
                    }`}
                    style={{ backgroundColor: color }}
                    title={color}
                    aria-label={`选择颜色 ${color}`}
                  />
                ))}
              </div>
            </div>

            {/* 操作按钮 */}
            <div className="flex justify-end pt-2">
              <button
                onClick={handleEditSave}
                className="px-5 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm
                           font-medium rounded-lg transition-colors"
              >
                保存
              </button>
            </div>
          </div>
        )}
      </Modal>
    </div>
  );
}
