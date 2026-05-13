// ============================================================
// 预设管理 — obs-multicam-review
// 保存/加载/删除/导入/导出预设
// ============================================================
import { useState, useCallback, useRef } from 'react';
import {
  List, Save, Upload, Download, Trash2, Loader2,
  AlertTriangle, Check, FolderOpen, FileJson,
} from 'lucide-react';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { Modal } from '../components/Modal';

// ── 类型 ──────────────────────────────────────────────

interface Preset {
  id: string;
  name: string;
  createdAt: string;
}

// ── 模拟数据 ──────────────────────────────────────────

const MOCK_PRESETS: Preset[] = [
  { id: 'pr-1', name: '双机位评测 - 默认', createdAt: '2025-01-15T10:30:00Z' },
  { id: 'pr-2', name: '单产品展示', createdAt: '2025-02-01T14:00:00Z' },
  { id: 'pr-3', name: '直播流输出', createdAt: '2025-03-10T09:15:00Z' },
];

// ── 骨架 ──────────────────────────────────────────────

function Skeleton() {
  return (
    <div className="flex items-center justify-center h-full">
      <Loader2 size={32} className="animate-spin text-slate-500" />
    </div>
  );
}

function ErrorState({ error }: { error: string }) {
  return (
    <div className="flex flex-col items-center justify-center h-full gap-3 text-slate-400">
      <AlertTriangle size={36} className="text-red-400" />
      <p className="text-sm">{error}</p>
    </div>
  );
}

function EmptyState() {
  return (
    <div className="flex flex-col items-center justify-center py-16 text-slate-500 gap-3">
      <List size={48} className="opacity-30" />
      <p className="text-sm">暂无预设</p>
    </div>
  );
}

// ── 格式化 ────────────────────────────────────────────

function formatDate(iso: string): string {
  try {
    const d = new Date(iso);
    return d.toLocaleString('zh-CN', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit',
    });
  } catch {
    return iso;
  }
}

// ── 页面主体 ──────────────────────────────────────────

export default function PresetsPage() {
  const [presets, setPresets] = useState<Preset[]>(MOCK_PRESETS);
  const [saving, setSaving] = useState(false);
  const [loading, setLoading] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [deleteTarget, setDeleteTarget] = useState<Preset | null>(null);

  // 新建对话框
  const [showSave, setShowSave] = useState(false);
  const [newName, setNewName] = useState('');

  // 文件导入
  const fileRef = useRef<HTMLInputElement>(null);

  // ── 保存当前设置 ──
  const handleSave = useCallback(async () => {
    if (!newName.trim()) return;
    setSaving(true);
    try {
      // await api POST /api/preset/save { name: newName }
      await new Promise(r => setTimeout(r, 400));
      const now = new Date().toISOString();
      setPresets(prev => {
        const existing = prev.find(p => p.name === newName.trim());
        if (existing) {
          return prev.map(p => p.name === newName.trim() ? { ...p, createdAt: now } : p);
        }
        return [{ id: `pr-${Date.now()}`, name: newName.trim(), createdAt: now }, ...prev];
      });
      setShowSave(false);
      setNewName('');
    } catch (err) {
      alert('保存失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setSaving(false);
    }
  }, [newName]);

  // ── 加载预设 ──
  const handleLoad = useCallback(async (preset: Preset) => {
    setLoading(preset.id);
    try {
      // await api POST /api/preset/load { presetId: preset.id }
      await new Promise(r => setTimeout(r, 300));
    } catch (err) {
      alert('加载失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setLoading(null);
    }
  }, []);

  // ── 删除预设 ──
  const handleDelete = useCallback(async () => {
    if (!deleteTarget) return;
    try {
      // await api DELETE /api/preset/${id}
      await new Promise(r => setTimeout(r, 200));
      setPresets(prev => prev.filter(p => p.id !== deleteTarget.id));
      setDeleteTarget(null);
    } catch (err) {
      alert('删除失败: ' + (err instanceof Error ? err.message : String(err)));
    }
  }, [deleteTarget]);

  // ── 导出预设 ──
  const handleExport = useCallback((preset: Preset) => {
    const blob = new Blob([JSON.stringify(preset, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `preset-${preset.name.replace(/\s+/g, '-')}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }, []);

  // ── 导入预设 ──
  const handleImport = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const data = JSON.parse(reader.result as string) as Preset;
        if (!data.name) throw new Error('无效的预设文件');
        const now = new Date().toISOString();
        setPresets(prev => [
          { id: `pr-${Date.now()}`, name: `${data.name} (导入)`, createdAt: now },
          ...prev,
        ]);
      } catch (err) {
        alert('导入失败: ' + (err instanceof Error ? err.message : '文件格式不正确'));
      }
    };
    reader.readAsText(file);
    // reset
    if (fileRef.current) fileRef.current.value = '';
  }, []);

  // ── 渲染 ──
  if (error) return <ErrorState error={error} />;

  return (
    <div className="max-w-3xl mx-auto space-y-5">
      {/* 页头 */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <List size={20} className="text-yellow-400" />
          预设管理
        </h2>
        <div className="flex items-center gap-2">
          {/* 导入 */}
          <button
            onClick={() => fileRef.current?.click()}
            className="flex items-center gap-1.5 px-3 py-2 text-xs text-slate-300
                       bg-slate-700 hover:bg-slate-600 rounded-lg transition-colors"
          >
            <Upload size={14} />
            导入
          </button>
          <input
            ref={fileRef}
            type="file"
            accept=".json"
            onChange={handleImport}
            className="hidden"
          />

          {/* 保存当前 */}
          <button
            onClick={() => { setNewName(''); setShowSave(true); }}
            className="flex items-center gap-1.5 px-4 py-2 text-xs font-medium text-white
                       bg-yellow-600 hover:bg-yellow-700 rounded-lg transition-colors"
          >
            <Save size={14} />
            保存当前设置
          </button>
        </div>
      </div>

      {/* 预设列表 */}
      {presets.length === 0 ? (
        <EmptyState />
      ) : (
        <div className="space-y-2">
          {presets.map(preset => {
            const isLoading = loading === preset.id;

            return (
              <div
                key={preset.id}
                className="bg-slate-800/50 backdrop-blur-sm border border-slate-700/60
                           rounded-xl px-4 py-3.5 flex items-center justify-between
                           hover:border-slate-600 transition-all"
              >
                {/* 信息 */}
                <div className="flex items-center gap-3 min-w-0">
                  <FileJson size={18} className="text-slate-500 shrink-0" />
                  <div className="min-w-0">
                    <div className="text-sm font-medium text-slate-200 truncate">
                      {preset.name}
                    </div>
                    <div className="text-[11px] text-slate-500">
                      {formatDate(preset.createdAt)}
                    </div>
                  </div>
                </div>

                {/* 操作 */}
                <div className="flex items-center gap-1.5 ml-4 shrink-0">
                  <button
                    onClick={() => handleLoad(preset)}
                    disabled={isLoading}
                    className="flex items-center gap-1 px-3 py-1.5 text-xs text-slate-300
                               bg-slate-700 hover:bg-slate-600 rounded-lg transition-colors
                               disabled:opacity-50"
                  >
                    {isLoading ? (
                      <Loader2 size={12} className="animate-spin" />
                    ) : (
                      <Check size={12} />
                    )}
                    加载
                  </button>
                  <button
                    onClick={() => handleExport(preset)}
                    className="p-1.5 text-slate-500 hover:text-slate-300 rounded-lg
                               transition-colors"
                    title="导出"
                  >
                    <Download size={14} />
                  </button>
                  <button
                    onClick={() => setDeleteTarget(preset)}
                    className="p-1.5 text-slate-600 hover:text-red-400 rounded-lg
                               transition-colors"
                    title="删除"
                  >
                    <Trash2 size={14} />
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      )}

      {/* 保存对话框 */}
      <Modal
        open={showSave}
        onClose={() => setShowSave(false)}
        title="保存预设"
        width="sm"
      >
        <div className="space-y-4">
          <div>
            <label className="block text-xs text-slate-500 uppercase tracking-wide mb-1">
              预设名称 *
            </label>
            <input
              value={newName}
              onChange={e => setNewName(e.target.value)}
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2
                         text-slate-200 text-sm focus:outline-none focus:border-yellow-500
                         transition-colors"
              placeholder="例如：双机位评测 - 默认"
              autoFocus
            />
          </div>
          <div className="flex justify-end pt-2">
            <button
              onClick={handleSave}
              disabled={!newName.trim() || saving}
              className="px-5 py-2 bg-yellow-600 hover:bg-yellow-700 disabled:bg-slate-700
                         disabled:text-slate-500 text-white text-sm font-medium rounded-lg
                         transition-colors"
            >
              {saving ? '保存中...' : '保存'}
            </button>
          </div>
        </div>
      </Modal>

      {/* 删除确认 */}
      <ConfirmDialog
        open={deleteTarget !== null}
        onClose={() => setDeleteTarget(null)}
        onConfirm={handleDelete}
        title="删除预设"
        message={`确定要删除预设「${deleteTarget?.name}」吗？此操作不可撤销。`}
        confirmLabel="删除"
        danger
      />
    </div>
  );
}
