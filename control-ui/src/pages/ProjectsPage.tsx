// ============================================================
// 项目管理页面 — obs-multicam-review
// ============================================================
import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { FolderKanban, Plus, Loader2, AlertTriangle, PackageOpen, Trash2 } from 'lucide-react';
import { useApi } from '../hooks/useApi';
import { fetchProjectList, createProject, deleteProject } from '../services/api-client';
import type { Project, ProjectCreateRequest } from '../types/api';
import { Modal } from '../components/Modal';
import { ConfirmDialog } from '../components/ConfirmDialog';

const STATUS_MAP: Record<string, { label: string; cls: string }> = {
  draft:     { label: '草稿',   cls: 'bg-slate-700 text-slate-300' },
  recording: { label: '录制中', cls: 'bg-blue-900/50 text-blue-300 border border-blue-700' },
  completed: { label: '已完成', cls: 'bg-green-900/50 text-green-300 border border-green-700' },
};

export default function ProjectsPage() {
  const navigate = useNavigate();
  const { data, loading, error, refresh } = useApi(fetchProjectList);
  const [showCreate, setShowCreate] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<Project | null>(null);
  const [form, setForm] = useState<ProjectCreateRequest>({
    name: '', fps: 60, resolutionX: 1920, resolutionY: 1080,
  });

  const handleCreate = async () => {
    if (!form.name.trim()) return;
    const project = await createProject(form);
    setShowCreate(false);
    setForm({ name: '', fps: 60, resolutionX: 1920, resolutionY: 1080 });
    navigate(`/products/${project.id}`);
  };

  const handleDelete = async () => {
    if (!deleteTarget) return;
    await deleteProject(deleteTarget.id);
    setDeleteTarget(null);
    refresh();
  };

  // Loading
  if (loading) {
    return (
      <div className="flex items-center justify-center h-full">
        <Loader2 size={32} className="animate-spin text-slate-500" />
      </div>
    );
  }

  // Error
  if (error) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3 text-slate-400">
        <AlertTriangle size={36} className="text-red-400" />
        <p className="text-sm">{error}</p>
        <button onClick={refresh} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm rounded-lg">
          重试
        </button>
      </div>
    );
  }

  const projects = data?.projects ?? [];

  return (
    <div className="max-w-4xl mx-auto space-y-4">
      {/* 页头 */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <FolderKanban size={20} className="text-blue-400" />
          项目管理
        </h2>
        <button
          onClick={() => setShowCreate(true)}
          className="flex items-center gap-1 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors"
        >
          <Plus size={16} /> 新建项目
        </button>
      </div>

      {/* 空状态 */}
      {projects.length === 0 ? (
        <div className="flex flex-col items-center justify-center py-16 text-slate-500 gap-3">
          <PackageOpen size={48} className="opacity-30" />
          <p className="text-sm">暂无项目，点击右上角"新建项目"开始</p>
        </div>
      ) : (
        /* 项目列表 */
        <div className="space-y-2">
          {projects.map((p) => {
            const st = STATUS_MAP[p.status] ?? STATUS_MAP.draft;
            return (
              <div
                key={p.id}
                className="group flex items-center justify-between bg-slate-800 border border-slate-700 rounded-lg px-4 py-3
                           hover:border-slate-600 cursor-pointer transition-colors"
                onClick={() => navigate(`/products/${p.id}`)}
              >
                <div className="flex items-center gap-4 min-w-0">
                  <span className="text-slate-100 font-medium truncate">{p.name}</span>
                  <span className={`text-xs px-2 py-0.5 rounded-full ${st.cls}`}>{st.label}</span>
                </div>
                <div className="flex items-center gap-4 text-xs text-slate-500">
                  <span>{p.resolutionX}×{p.resolutionY}</span>
                  <span>{p.fps}fps</span>
                  <span>{new Date(p.createdAt).toLocaleDateString('zh-CN')}</span>
                  <button
                    onClick={(e) => { e.stopPropagation(); setDeleteTarget(p); }}
                    className="opacity-0 group-hover:opacity-100 text-slate-500 hover:text-red-400 transition-all p-1"
                  >
                    <Trash2 size={14} />
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      )}

      {/* 新建 Modal */}
      <Modal open={showCreate} onClose={() => setShowCreate(false)} title="新建项目" width="sm">
        <div className="space-y-3">
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">项目名称 *</label>
            <input
              value={form.name}
              onChange={(e) => setForm({ ...form, name: e.target.value })}
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm
                         focus:outline-none focus:border-blue-500"
              placeholder="2026春季手机横评"
              autoFocus
            />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs text-slate-500 uppercase mb-1">FPS</label>
              <input
                type="number" value={form.fps}
                onChange={(e) => setForm({ ...form, fps: Number(e.target.value) })}
                className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm"
              />
            </div>
            <div className="grid grid-cols-2 gap-2">
              <div>
                <label className="block text-xs text-slate-500 uppercase mb-1">宽</label>
                <input
                  type="number" value={form.resolutionX}
                  onChange={(e) => setForm({ ...form, resolutionX: Number(e.target.value) })}
                  className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm"
                />
              </div>
              <div>
                <label className="block text-xs text-slate-500 uppercase mb-1">高</label>
                <input
                  type="number" value={form.resolutionY}
                  onChange={(e) => setForm({ ...form, resolutionY: Number(e.target.value) })}
                  className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm"
                />
              </div>
            </div>
          </div>
          <div className="flex justify-end pt-2">
            <button
              onClick={handleCreate}
              disabled={!form.name.trim()}
              className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:bg-slate-700 disabled:text-slate-500
                         text-white text-sm font-medium rounded-lg transition-colors"
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
        title="删除项目"
        message={`确定要删除「${deleteTarget?.name}」吗？该项目下的所有产品也会被删除。`}
        confirmLabel="删除"
        danger
      />
    </div>
  );
}
