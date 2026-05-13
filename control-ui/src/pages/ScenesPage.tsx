// ============================================================
// 场景管理页 — obs-multicam-review
// ============================================================
import { useState, useCallback } from 'react';
import {
  Clapperboard, Plus, Loader2, AlertTriangle,
  Trash2, Lock, Check, Grid3X3, Zap,
} from 'lucide-react';
import { useApi } from '../hooks/useApi';
import * as api from '../services/api-client';
import { Modal } from '../components/Modal';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { SceneListItem } from '../types/api';

// ── 常量 ──────────────────────────────────────────────

const LAYOUT_LABELS: Record<string, string> = {
  'fullscreen': '全屏',
  'pip':        '画中画',
  'split-2':    '二分屏',
  'split-3':    '三分屏',
  'split-4':    '四分屏',
};

const LAYOUT_OPTIONS = Object.entries(LAYOUT_LABELS).map(([value, label]) => ({ value, label }));

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

function EmptyState() {
  return (
    <div className="flex flex-col items-center justify-center py-16 text-slate-500 gap-3">
      <Grid3X3 size={48} className="opacity-30" />
      <p className="text-sm">暂无场景</p>
    </div>
  );
}

// ── 当前场景卡片 ──────────────────────────────────────

function CurrentSceneCard({
  scene,
  onSwitch,
  switching,
}: {
  scene: SceneListItem;
  onSwitch: (name: string) => void;
  switching: boolean;
}) {
  return (
    <div className="relative rounded-xl overflow-hidden">
      {/* 呼吸动画边框 */}
      <style>{`
        @keyframes breathe {
          0%, 100% { box-shadow: 0 0 0 0 rgba(34, 197, 94, 0.4); }
          50%      { box-shadow: 0 0 0 6px rgba(34, 197, 94, 0.08); }
        }
        .animate-breathe {
          animation: breathe 2.5s ease-in-out infinite;
        }
      `}</style>

      <div className="animate-breathe rounded-xl border-2 border-green-500/70 bg-slate-800/80
                      backdrop-blur-sm px-5 py-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3 min-w-0">
            {/* 当前场景指示器 */}
            <div className="relative">
              <div className="w-2.5 h-2.5 bg-green-400 rounded-full" />
              <div className="absolute inset-0 w-2.5 h-2.5 bg-green-400 rounded-full animate-ping opacity-30" />
            </div>

            <div className="min-w-0">
              <div className="flex items-center gap-2">
                <h3 className="text-base font-bold text-green-300 truncate">
                  {scene.name}
                </h3>
                <span className="text-xs px-1.5 py-0.5 rounded bg-green-900/40 text-green-300 border border-green-700/50">
                  当前
                </span>
              </div>
              <div className="flex items-center gap-2 mt-1 text-xs text-slate-400">
                <LayoutBadge layoutType={scene.layoutType} />
                {scene.isBuiltin && <Lock size={11} className="text-slate-500" />}
              </div>
            </div>
          </div>

          {/* 源标签 */}
          <div className="hidden sm:flex items-center gap-1.5 flex-wrap max-w-[200px]">
            {scene.sources.slice(0, 4).map(s => (
              <span
                key={s}
                className="text-[11px] text-slate-500 bg-slate-700/50 rounded px-1.5 py-0.5 truncate max-w-[80px]"
                title={s}
              >
                {s}
              </span>
            ))}
            {scene.sources.length > 4 && (
              <span className="text-[11px] text-slate-600">+{scene.sources.length - 4}</span>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

// ── 布局类型徽章 ──────────────────────────────────────

function LayoutBadge({ layoutType }: { layoutType: string }) {
  const label = LAYOUT_LABELS[layoutType] || layoutType;
  return (
    <span className="text-[11px] px-1.5 py-0.5 rounded bg-slate-700/50 text-slate-400">
      {label}
    </span>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function ScenesPage() {
  const { data, loading, error, refresh } = useApi(api.fetchSceneList);
  const { data: sourceData } = useApi(api.fetchSourceList);

  const [showCreate, setShowCreate] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<SceneListItem | null>(null);
  const [switching, setSwitching] = useState<string | null>(null);
  const [highlightedScene, setHighlightedScene] = useState<string | null>(null);

  // 新建表单
  const [newName, setNewName] = useState('');
  const [newLayout, setNewLayout] = useState('fullscreen');
  const [selectedSources, setSelectedSources] = useState<string[]>([]);

  const sceneList = data?.scenes ?? [];
  const currentSceneName = data?.currentScene ?? '';
  const allSourceNames = sourceData?.sources?.map(s => s.alias || s.obsName) ?? [];

  // ── 切换场景 ──
  const handleSwitch = useCallback(async (sceneName: string) => {
    if (switching || sceneName === currentSceneName) return;
    setSwitching(sceneName);
    try {
      await api.switchScene({ sceneName });
      setHighlightedScene(sceneName);
      setTimeout(() => setHighlightedScene(null), 1500);
      refresh();
    } catch (err) {
      alert('切换失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setSwitching(null);
    }
  }, [switching, currentSceneName, refresh]);

  // ── 新建场景 ──
  const handleCreate = async () => {
    if (!newName.trim()) return;
    try {
      await api.createScene({
        sceneName: newName.trim(),
        layoutType: newLayout,
        sourceNames: selectedSources,
      });
      setShowCreate(false);
      resetForm();
      refresh();
    } catch (err) {
      alert('创建失败: ' + (err instanceof Error ? err.message : String(err)));
    }
  };

  // ── 删除场景 ──
  const handleDelete = async () => {
    if (!deleteTarget) return;
    try {
      await api.deleteScene(deleteTarget.name);
      setDeleteTarget(null);
      refresh();
    } catch (err) {
      alert('删除失败: ' + (err instanceof Error ? err.message : String(err)));
    }
  };

  const resetForm = () => {
    setNewName('');
    setNewLayout('fullscreen');
    setSelectedSources([]);
  };

  const toggleSource = (name: string) => {
    setSelectedSources(prev =>
      prev.includes(name) ? prev.filter(n => n !== name) : [...prev, name],
    );
  };

  // ── 渲染 ──
  if (loading) return <Skeleton />;
  if (error) return <ErrorState error={error} onRetry={refresh} />;

  return (
    <div className="max-w-4xl mx-auto space-y-5">
      {/* 页头 */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <Clapperboard size={20} className="text-purple-400" />
          场景管理
        </h2>
        <button
          onClick={() => { resetForm(); setShowCreate(true); }}
          className="flex items-center gap-1.5 px-4 py-2 bg-purple-600 hover:bg-purple-700
                     text-white text-sm font-medium rounded-lg transition-colors"
        >
          <Plus size={16} /> 新建场景
        </button>
      </div>

      {/* 空状态 */}
      {sceneList.length === 0 ? (
        <EmptyState />
      ) : (
        <>
          {/* 当前场景卡片 */}
          {currentSceneName && (() => {
            const current = sceneList.find(s => s.name === currentSceneName);
            if (!current) return null;
            return (
              <CurrentSceneCard
                scene={current}
                onSwitch={handleSwitch}
                switching={switching === current.name}
              />
            );
          })()}

          {/* 场景网格 */}
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
            {sceneList.map(scene => {
              const isCurrent = scene.name === currentSceneName;
              const isHighlighted = highlightedScene === scene.name;

              return (
                <div
                  key={scene.name}
                  className={`bg-slate-800/60 backdrop-blur-sm border rounded-xl px-4 py-3.5
                              transition-all duration-300 ${
                                isHighlighted
                                  ? 'border-green-400 bg-green-900/20 shadow-lg shadow-green-500/10 scale-[1.02]'
                                  : isCurrent
                                    ? 'border-green-500/50'
                                    : 'border-slate-700/60 hover:border-slate-600'
                              }`}
                >
                  {/* 标题行 */}
                  <div className="flex items-center justify-between mb-2">
                    <div className="flex items-center gap-2 min-w-0">
                      {/* 当前指示点 */}
                      {isCurrent && (
                        <div className="w-2 h-2 rounded-full bg-green-400 shrink-0" />
                      )}
                      <h3 className="text-sm font-semibold text-slate-200 truncate">
                        {scene.name}
                      </h3>
                      {isCurrent && (
                        <span className="text-[10px] px-1.5 py-0.5 rounded bg-green-900/40 text-green-300 border border-green-700/50 shrink-0">
                          当前
                        </span>
                      )}
                      {scene.isBuiltin && (
                        <Lock size={11} className="text-slate-500 shrink-0" title="内置场景" />
                      )}
                    </div>

                    <LayoutBadge layoutType={scene.layoutType} />
                  </div>

                  {/* 源标签云 */}
                  {scene.sources.length > 0 && (
                    <div className="flex flex-wrap gap-1 mb-3">
                      {scene.sources.slice(0, 6).map(s => (
                        <span
                          key={s}
                          className="text-[11px] text-slate-400 bg-slate-700/40 rounded px-1.5 py-0.5 truncate max-w-[100px]"
                          title={s}
                        >
                          {s}
                        </span>
                      ))}
                      {scene.sources.length > 6 && (
                        <span className="text-[11px] text-slate-600">+{scene.sources.length - 6}</span>
                      )}
                    </div>
                  )}

                  {/* 操作按钮 */}
                  <div className="flex items-center gap-2">
                    <button
                      onClick={() => handleSwitch(scene.name)}
                      disabled={switching === scene.name || isCurrent}
                      className={`flex items-center gap-1 px-3 py-1.5 text-xs font-medium rounded-lg
                                  transition-all duration-150 ${
                                    isCurrent
                                      ? 'bg-green-600/30 text-green-300 cursor-default'
                                      : 'bg-slate-700 hover:bg-slate-600 text-slate-300 hover:text-white'
                                  } disabled:opacity-50`}
                    >
                      {switching === scene.name ? (
                        <Loader2 size={12} className="animate-spin" />
                      ) : isCurrent ? (
                        <Check size={12} />
                      ) : (
                        <Zap size={12} />
                      )}
                      {switching === scene.name ? '切换中…' : isCurrent ? '当前场景' : '切换'}
                    </button>

                    {!scene.isBuiltin && (
                      <button
                        onClick={() => setDeleteTarget(scene)}
                        className="p-1.5 text-slate-600 hover:text-red-400 hover:bg-red-400/10
                                   rounded-lg transition-all ml-auto"
                        title="删除场景"
                      >
                        <Trash2 size={14} />
                      </button>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        </>
      )}

      {/* 新建 Modal */}
      <Modal
        open={showCreate}
        onClose={() => setShowCreate(false)}
        title="新建场景"
        width="sm"
      >
        <div className="space-y-4">
          {/* 场景名称 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase tracking-wide mb-1">
              场景名称 *
            </label>
            <input
              value={newName}
              onChange={e => setNewName(e.target.value)}
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2
                         text-slate-200 text-sm focus:outline-none focus:border-purple-500
                         transition-colors"
              placeholder="例如：双机位评测"
              autoFocus
            />
          </div>

          {/* 布局类型 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase tracking-wide mb-1">
              布局类型
            </label>
            <select
              value={newLayout}
              onChange={e => setNewLayout(e.target.value)}
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2
                         text-slate-200 text-sm focus:outline-none focus:border-purple-500
                         transition-colors appearance-none cursor-pointer"
            >
              {LAYOUT_OPTIONS.map(opt => (
                <option key={opt.value} value={opt.value}>
                  {opt.label}
                </option>
              ))}
            </select>
          </div>

          {/* 源选择 */}
          {allSourceNames.length > 0 && (
            <div>
              <label className="block text-xs text-slate-500 uppercase tracking-wide mb-2">
                包含的源（多选）
              </label>
              <div className="flex flex-wrap gap-2 max-h-32 overflow-y-auto">
                {allSourceNames.map(name => (
                  <button
                    key={name}
                    onClick={() => toggleSource(name)}
                    className={`text-xs px-2.5 py-1 rounded-full border transition-all ${
                      selectedSources.includes(name)
                        ? 'bg-purple-600/30 border-purple-500 text-purple-200'
                        : 'bg-slate-800 border-slate-700 text-slate-400 hover:border-slate-600'
                    }`}
                  >
                    {selectedSources.includes(name) && (
                      <Check size={10} className="inline mr-1" />
                    )}
                    {name}
                  </button>
                ))}
              </div>
            </div>
          )}

          {allSourceNames.length === 0 && (
            <p className="text-xs text-slate-600">暂无可用输入源，请先在 OBS 中添加源</p>
          )}

          {/* 操作按钮 */}
          <div className="flex justify-end pt-2">
            <button
              onClick={handleCreate}
              disabled={!newName.trim()}
              className="px-5 py-2 bg-purple-600 hover:bg-purple-700 disabled:bg-slate-700
                         disabled:text-slate-500 text-white text-sm font-medium rounded-lg
                         transition-colors"
            >
              创建
            </button>
          </div>
        </div>
      </Modal>

      {/* 删除确认 */}
      <ConfirmDialog
        open={deleteTarget !== null}
        onClose={() => setDeleteTarget(null)}
        onConfirm={handleDelete}
        title="删除场景"
        message={`确定要删除场景「${deleteTarget?.name}」吗？此操作不可撤销。`}
        confirmLabel="删除"
        danger
      />
    </div>
  );
}
