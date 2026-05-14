// ============================================================
// 维度模板管理页面 — obs-multicam-review
// 创建/查看/删除评分维度模板
// ============================================================
import { useState, useCallback } from 'react';
import {
  Plus, Trash2, Loader2, AlertTriangle,
  Layers, GripVertical, Shield, Lock,
} from 'lucide-react';
import { useApi } from '../hooks/useApi';
import * as api from '../services/api-client';
import { Modal } from '../components/Modal';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { DimensionTemplate, DimensionItem } from '../types/api';

// ============ 维度项表单条目 ============
interface DimItemForm {
  key: string; // 用于 React key
  dimKey: string;
  label: string;
  weight: number;
  maxScore: number;
}

// ============ 创建模板表单 ============
interface CreateTemplateForm {
  name: string;
  items: DimItemForm[];
}

let dimItemCounter = 0;
function newDimItem(): DimItemForm {
  dimItemCounter += 1;
  return {
    key: `new_${dimItemCounter}_${Date.now()}`,
    dimKey: '',
    label: '',
    weight: 1,
    maxScore: 10,
  };
}

const emptyForm: CreateTemplateForm = {
  name: '',
  items: [newDimItem()],
};

// ============ 页面组件 ============
export default function DimensionsPage() {
  const fetchTemplates = useCallback(() => api.fetchDimTemplates(), []);
  const { data: templates, loading, error, refresh } = useApi(fetchTemplates);

  // 创建模态框
  const [showCreate, setShowCreate] = useState(false);
  const [form, setForm] = useState<CreateTemplateForm>(emptyForm);
  const [submitting, setSubmitting] = useState(false);
  const [submitError, setSubmitError] = useState('');

  // 删除确认
  const [deleteTarget, setDeleteTarget] = useState<DimensionTemplate | null>(null);

  // ========== 重置表单 ==========
  function resetForm() {
    dimItemCounter = 0;
    setForm({ ...emptyForm, items: [newDimItem()] });
    setSubmitError('');
  }

  function openCreate() {
    resetForm();
    setShowCreate(true);
  }

  // ========== 表单字段操作 ==========
  function updateName(name: string) {
    setForm((prev) => ({ ...prev, name }));
  }

  function updateItem(index: number, field: keyof DimItemForm, value: string | number) {
    setForm((prev) => {
      const items = prev.items.map((item, i) =>
        i === index ? { ...item, [field]: value } : item,
      );
      return { ...prev, items };
    });
  }

  function addItem() {
    setForm((prev) => ({ ...prev, items: [...prev.items, newDimItem()] }));
  }

  function removeItem(index: number) {
    setForm((prev) => {
      if (prev.items.length <= 1) return prev;
      return { ...prev, items: prev.items.filter((_, i) => i !== index) };
    });
  }

  // ========== 提交创建 ==========
  async function handleCreate() {
    if (!form.name.trim()) {
      setSubmitError('模板名称为必填项');
      return;
    }

    // 验证维度项
    const validItems = form.items.filter((item) => item.dimKey.trim() && item.label.trim());
    if (validItems.length === 0) {
      setSubmitError('至少需要一个维度项（填写 key 和 label）');
      return;
    }

    // 检查 dimKey 重复
    const keys = validItems.map((i) => i.dimKey.trim());
    const dupes = keys.filter((k, idx) => keys.indexOf(k) !== idx);
    if (dupes.length > 0) {
      setSubmitError(`维度键重复: ${dupes.join(', ')}`);
      return;
    }

    setSubmitting(true);
    setSubmitError('');
    try {
      await api.createDimTemplate({
        name: form.name.trim(),
        items: validItems.map((item, idx) => ({
          dimKey: item.dimKey.trim(),
          label: item.label.trim(),
          weight: item.weight > 0 ? item.weight : 1,
          maxScore: item.maxScore > 0 ? item.maxScore : 10,
          sortOrder: idx,
        })),
      });
      setShowCreate(false);
      refresh();
    } catch (e) {
      setSubmitError(e instanceof Error ? e.message : '创建失败');
    } finally {
      setSubmitting(false);
    }
  }

  // ========== 删除模板 ==========
  async function handleDelete() {
    if (!deleteTarget) return;
    try {
      await api.deleteDimTemplate(deleteTarget.id);
      setDeleteTarget(null);
      refresh();
    } catch {
      // 静默失败
    }
  }

  // ========== 渲染 ==========

  // 加载态
  if (loading && !templates) {
    return (
      <div className="flex items-center justify-center h-full">
        <Loader2 size={32} className="animate-spin text-slate-500" />
      </div>
    );
  }

  // 错误态
  if (error && !templates) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3">
        <AlertTriangle size={36} className="text-red-400" />
        <p className="text-red-400 text-sm">{error}</p>
        <button
          onClick={refresh}
          className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm rounded-lg"
        >
          重试
        </button>
      </div>
    );
  }

  const templateList = templates ?? [];

  return (
    <div className="space-y-4">
      {/* ===== 页头 ===== */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <Layers size={20} className="text-indigo-400" />
          评分维度模板
        </h2>
        <button
          onClick={openCreate}
          className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors"
        >
          <Plus size={16} />
          新建模板
        </button>
      </div>

      {/* ===== 空状态 ===== */}
      {templateList.length === 0 ? (
        <div className="flex flex-col items-center justify-center py-20 text-slate-500">
          <Layers size={48} className="mb-3 opacity-30" />
          <p className="text-sm">暂无维度模板</p>
          <p className="text-xs mt-1 text-slate-600">
            维度模板定义了产品的评分维度（如画质、手感、性价比等）
          </p>
          <button
            onClick={openCreate}
            className="mt-3 flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors"
          >
            <Plus size={16} /> 创建第一个模板
          </button>
        </div>
      ) : (
        /* ===== 模板列表 ===== */
        <div className="space-y-3">
          {templateList.map((tmpl) => (
            <TemplateCard
              key={tmpl.id}
              template={tmpl}
              onDelete={() => setDeleteTarget(tmpl)}
            />
          ))}
        </div>
      )}

      {/* ===== 创建模板模态框 ===== */}
      <Modal
        open={showCreate}
        onClose={() => setShowCreate(false)}
        title="新建维度模板"
        width="lg"
      >
        <div className="space-y-4">
          {/* 模板名称 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">
              模板名称 <span className="text-red-400">*</span>
            </label>
            <input
              value={form.name}
              onChange={(e) => updateName(e.target.value)}
              placeholder="如：手机横评标准维度"
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm focus:outline-none focus:border-blue-500"
              autoFocus
            />
          </div>

          {/* 维度项列表 */}
          <div>
            <div className="flex items-center justify-between mb-2">
              <label className="text-xs text-slate-500 uppercase">
                维度项 <span className="text-red-400">*</span>
              </label>
              <button
                onClick={addItem}
                className="text-xs text-blue-400 hover:text-blue-300 flex items-center gap-1"
              >
                <Plus size={12} /> 添加维度
              </button>
            </div>

            <div className="space-y-2 max-h-60 overflow-y-auto">
              {form.items.map((item, index) => (
                <div
                  key={item.key}
                  className="flex items-center gap-2 bg-slate-800 border border-slate-700 rounded-lg px-3 py-2"
                >
                  <GripVertical size={14} className="text-slate-600 flex-shrink-0" />

                  {/* dimKey */}
                  <input
                    value={item.dimKey}
                    onChange={(e) => updateItem(index, 'dimKey', e.target.value)}
                    placeholder="键名"
                    className="w-24 bg-slate-900 border border-slate-600 rounded px-2 py-1.5 text-slate-200 text-xs font-mono focus:outline-none focus:border-blue-500"
                    title="唯一标识符，如 image_quality"
                  />

                  {/* label */}
                  <input
                    value={item.label}
                    onChange={(e) => updateItem(index, 'label', e.target.value)}
                    placeholder="显示名称"
                    className="flex-1 bg-slate-900 border border-slate-600 rounded px-2 py-1.5 text-slate-200 text-xs focus:outline-none focus:border-blue-500"
                    title="用户可见名称，如 画质"
                  />

                  {/* weight */}
                  <div className="flex items-center gap-1">
                    <label className="text-[10px] text-slate-600">权重</label>
                    <input
                      type="number"
                      min={0.1}
                      max={10}
                      step={0.1}
                      value={item.weight}
                      onChange={(e) => updateItem(index, 'weight', parseFloat(e.target.value) || 1)}
                      className="w-14 bg-slate-900 border border-slate-600 rounded px-1.5 py-1.5 text-slate-200 text-xs text-center focus:outline-none focus:border-blue-500"
                    />
                  </div>

                  {/* maxScore */}
                  <div className="flex items-center gap-1">
                    <label className="text-[10px] text-slate-600">满分</label>
                    <input
                      type="number"
                      min={1}
                      max={100}
                      step={1}
                      value={item.maxScore}
                      onChange={(e) => updateItem(index, 'maxScore', parseInt(e.target.value, 10) || 10)}
                      className="w-14 bg-slate-900 border border-slate-600 rounded px-1.5 py-1.5 text-slate-200 text-xs text-center focus:outline-none focus:border-blue-500"
                    />
                  </div>

                  {/* 删除维度项 */}
                  <button
                    onClick={() => removeItem(index)}
                    disabled={form.items.length <= 1}
                    className="p-1 text-slate-500 hover:text-red-400 disabled:opacity-30 disabled:hover:text-slate-500 transition-colors flex-shrink-0"
                    title="移除维度"
                  >
                    <Trash2 size={14} />
                  </button>
                </div>
              ))}
            </div>

            <p className="text-xs text-slate-600 mt-1.5">
              dimKey 为唯一标识符（英文+下划线），label 为显示名称，权重影响加权总分
            </p>
          </div>

          {/* 错误 & 按钮 */}
          {submitError && (
            <p className="text-red-400 text-xs">{submitError}</p>
          )}
          <div className="flex justify-end gap-3 pt-2">
            <button
              onClick={() => setShowCreate(false)}
              className="px-4 py-2 text-sm text-slate-400 hover:text-slate-200 hover:bg-slate-800 rounded-lg transition-colors"
            >
              取消
            </button>
            <button
              onClick={handleCreate}
              disabled={submitting}
              className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors disabled:opacity-50"
            >
              {submitting ? '创建中...' : '创建模板'}
            </button>
          </div>
        </div>
      </Modal>

      {/* ===== 删除确认 ===== */}
      <ConfirmDialog
        open={!!deleteTarget}
        onClose={() => setDeleteTarget(null)}
        onConfirm={handleDelete}
        title="删除维度模板"
        message={`确定要删除「${deleteTarget?.name}」吗？已绑定此模板的产品将失去评分维度。此操作不可撤销。`}
        confirmLabel="删除"
        danger
      />
    </div>
  );
}

// ================================================================
// 模板卡片子组件
// ================================================================
function TemplateCard({
  template,
  onDelete,
}: {
  template: DimensionTemplate;
  onDelete: () => void;
}) {
  return (
    <div className="bg-slate-800 border border-slate-700 rounded-lg p-4">
      <div className="flex items-start justify-between mb-3">
        <div className="flex items-center gap-2">
          <h3 className="text-slate-100 font-semibold text-sm">{template.name}</h3>
          {template.isBuiltin && (
            <span className="flex items-center gap-1 text-[10px] text-indigo-400 bg-indigo-900/30 border border-indigo-700/50 rounded-full px-2 py-0.5">
              <Shield size={10} />
              内置
            </span>
          )}
        </div>
        {template.isBuiltin ? (
          <button
            disabled
            className="p-1.5 text-slate-600 cursor-not-allowed"
            title="内置模板不可删除"
          >
            <Lock size={14} />
          </button>
        ) : (
          <button
            onClick={onDelete}
            className="p-1.5 text-slate-500 hover:text-red-400 hover:bg-slate-700 rounded transition-colors"
            title="删除模板"
          >
            <Trash2 size={14} />
          </button>
        )}
      </div>

      {/* 维度项摘要 */}
      <div className="flex flex-wrap gap-1.5">
        {/* 加载模板的 items 需要额外请求，此处展示模板基本信息 */}
        <span className="text-xs text-slate-500">
          创建于 {new Date(template.createdAt).toLocaleDateString('zh-CN')}
        </span>
      </div>
    </div>
  );
}
