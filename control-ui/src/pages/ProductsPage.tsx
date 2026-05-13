// ============================================================
// 产品管理页面 — obs-multicam-review
// 展示/添加/编辑/删除项目下的产品
// ============================================================
import { useState, useCallback, useEffect } from 'react';
import { useParams, Link } from 'react-router-dom';
import { Plus, Edit, Trash2, ArrowLeft, Package } from 'lucide-react';
import { useApi } from '../hooks/useApi';
import * as api from '../services/api-client';
import { Modal } from '../components/Modal';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type { Product, ProductCreateRequest, ProductUpdateRequest } from '../types/api';

// ============ 表单状态 ============
interface ProductForm {
  brand: string;
  model: string;
  price: string;
  color: string;
  colorHex: string;
  specJson: string;
  imagePath: string;
  notes: string;
}

const emptyForm: ProductForm = {
  brand: '',
  model: '',
  price: '',
  color: '',
  colorHex: '',
  specJson: '',
  imagePath: '',
  notes: '',
};

// ============ 价格解析 ============
function parsePriceValue(text: string): number {
  const cleaned = text.replace(/¥|,/g, '').trim();
  const value = parseInt(cleaned, 10);
  return isNaN(value) ? 0 : value;
}

// ============ specJson 摘要 ============
function summarizeSpec(specJson: string): string {
  if (!specJson) return '';
  try {
    const items: Array<{ key: string; val: string }> = JSON.parse(specJson);
    if (!Array.isArray(items)) return '';
    return items.map((s) => `${s.key}:${s.val}`).join(' · ');
  } catch {
    return '';
  }
}

// ============ 页面组件 ============
export default function ProductsPage() {
  const { projectId } = useParams<{ projectId: string }>();
  const [projectName, setProjectName] = useState('');

  // 获取项目名
  useEffect(() => {
    if (!projectId) return;
    api.fetchProjectList()
      .then((res) => {
        const p = res.projects.find((proj) => proj.id === projectId);
        if (p) setProjectName(p.name);
      })
      .catch(() => {});
  }, [projectId]);

  // 获取产品列表
  const fetchProducts = useCallback(
    () => api.fetchProductList(projectId!).then((r) => r.products),
    [projectId],
  );
  const { data: products, loading, error, refresh } = useApi(fetchProducts, [projectId]);

  // 模态框 & 表单状态
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<Product | null>(null);
  const [form, setForm] = useState<ProductForm>(emptyForm);
  const [submitting, setSubmitting] = useState(false);
  const [submitError, setSubmitError] = useState('');

  // 删除确认
  const [deleteTarget, setDeleteTarget] = useState<Product | null>(null);

  // ========== 打开添加/编辑模态框 ==========
  function openCreate() {
    setEditing(null);
    setForm(emptyForm);
    setSubmitError('');
    setModalOpen(true);
  }

  function openEdit(product: Product) {
    setEditing(product);
    setForm({
      brand: product.brand,
      model: product.model,
      price: product.price,
      color: product.color,
      colorHex: product.colorHex,
      specJson: product.specJson,
      imagePath: product.imagePath,
      notes: product.notes,
    });
    setSubmitError('');
    setModalOpen(true);
  }

  function closeModal() {
    setModalOpen(false);
    setEditing(null);
    setSubmitError('');
  }

  // ========== 表单字段更新 ==========
  function updateField<K extends keyof ProductForm>(key: K, value: string) {
    setForm((prev) => ({ ...prev, [key]: value }));
  }

  // ========== 提交表单 ==========
  async function handleSubmit() {
    if (!form.brand.trim() || !form.model.trim()) {
      setSubmitError('品牌和型号为必填项');
      return;
    }

    const priceValue = parsePriceValue(form.price);

    const commonFields = {
      brand: form.brand.trim(),
      model: form.model.trim(),
      price: form.price.trim() || undefined,
      priceValue: priceValue > 0 ? priceValue : undefined,
      color: form.color.trim() || undefined,
      colorHex: form.colorHex.trim() || undefined,
      specJson: form.specJson.trim() || undefined,
      imagePath: form.imagePath.trim() || undefined,
      notes: form.notes.trim() || undefined,
    };

    setSubmitting(true);
    setSubmitError('');
    try {
      if (editing) {
        await api.updateProduct(editing.id, commonFields as ProductUpdateRequest);
      } else {
        await api.createProduct({
          projectId: projectId!,
          ...commonFields,
        } as ProductCreateRequest);
      }
      closeModal();
      refresh();
    } catch (e) {
      setSubmitError(e instanceof Error ? e.message : '提交失败');
    } finally {
      setSubmitting(false);
    }
  }

  // ========== 删除 ==========
  async function handleDelete() {
    if (!deleteTarget) return;
    try {
      await api.deleteProduct(deleteTarget.id);
      setDeleteTarget(null);
      refresh();
    } catch {
      // 静默失败 — 可以后续加 toast
    }
  }

  // ========== 渲染 ==========

  // 加载态
  if (loading && !products) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-slate-500 text-sm">加载中...</div>
      </div>
    );
  }

  // 错误态
  if (error && !products) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3">
        <p className="text-red-400 text-sm">{error}</p>
        <button
          onClick={refresh}
          className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium"
        >
          重试
        </button>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* ===== 顶部信息栏 ===== */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Link
            to="/projects"
            className="flex items-center gap-1 text-slate-400 hover:text-slate-200 transition-colors text-sm"
          >
            <ArrowLeft size={16} />
            返回项目列表
          </Link>
          <span className="text-slate-600">|</span>
          <h1 className="text-lg font-bold text-slate-100">
            {projectName || projectId}
          </h1>
          <span className="text-xs text-slate-500 bg-slate-800 rounded px-2 py-0.5">
            {(products ?? []).length} 款产品
          </span>
        </div>
        <button
          onClick={openCreate}
          className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium transition-colors"
        >
          <Plus size={16} />
          添加产品
        </button>
      </div>

      {/* ===== 产品卡片网格 ===== */}
      {(!products || products.length === 0) ? (
        <div className="flex flex-col items-center justify-center py-20 text-slate-500">
          <Package size={48} className="mb-3 opacity-40" />
          <p className="text-sm">暂无产品</p>
          <p className="text-xs mt-1">点击上方「添加产品」开始录入</p>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3">
          {products.map((product) => {
            const specSummary = summarizeSpec(product.specJson);
            return (
              <div
                key={product.id}
                className="bg-slate-800 border border-slate-700 rounded-lg p-4 flex flex-col gap-2.5"
              >
                {/* 品牌 + 型号 */}
                <div className="flex items-start justify-between">
                  <div>
                    <h3 className="text-slate-100 font-bold text-sm">
                      {product.brand}
                      {product.model && (
                        <span className="text-slate-400 font-normal ml-1">
                          {product.model}
                        </span>
                      )}
                    </h3>
                  </div>
                  {/* 操作按钮 */}
                  <div className="flex gap-1">
                    <button
                      onClick={() => openEdit(product)}
                      className="p-1.5 text-slate-500 hover:text-blue-400 hover:bg-slate-700 rounded transition-colors"
                      title="编辑"
                    >
                      <Edit size={14} />
                    </button>
                    <button
                      onClick={() => setDeleteTarget(product)}
                      className="p-1.5 text-slate-500 hover:text-red-400 hover:bg-slate-700 rounded transition-colors"
                      title="删除"
                    >
                      <Trash2 size={14} />
                    </button>
                  </div>
                </div>

                {/* 颜色标签 */}
                {product.color && (
                  <div className="flex items-center gap-2">
                    {product.colorHex && (
                      <span
                        className="inline-block w-3.5 h-3.5 rounded-full border border-slate-600"
                        style={{ backgroundColor: product.colorHex }}
                      />
                    )}
                    <span className="text-xs text-slate-400">{product.color}</span>
                    {product.colorHex && (
                      <span className="text-xs text-slate-500 font-mono">
                        {product.colorHex}
                      </span>
                    )}
                  </div>
                )}

                {/* 价格 */}
                {product.price && (
                  <p className="text-sm text-slate-200 font-semibold">
                    {product.price}
                  </p>
                )}

                {/* 规格摘要 */}
                {specSummary && (
                  <p className="text-xs text-slate-400 leading-relaxed">
                    {specSummary}
                  </p>
                )}

                {/* 备注 */}
                {product.notes && (
                  <p className="text-xs text-slate-500 italic truncate">
                    {product.notes}
                  </p>
                )}
              </div>
            );
          })}
        </div>
      )}

      {/* ===== 添加/编辑模态框 ===== */}
      <Modal
        open={modalOpen}
        onClose={closeModal}
        title={editing ? '编辑产品' : '添加产品'}
        width="lg"
      >
        <div className="space-y-4">
          {/* 品牌 & 型号 */}
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs text-slate-500 uppercase mb-1">
                品牌 <span className="text-red-400">*</span>
              </label>
              <input
                value={form.brand}
                onChange={(e) => updateField('brand', e.target.value)}
                placeholder="如: Huawei"
                className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs text-slate-500 uppercase mb-1">
                型号 <span className="text-red-400">*</span>
              </label>
              <input
                value={form.model}
                onChange={(e) => updateField('model', e.target.value)}
                placeholder="如: Mate 70 Pro"
                className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500"
              />
            </div>
          </div>

          {/* 价格 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">价格</label>
            <input
              value={form.price}
              onChange={(e) => updateField('price', e.target.value)}
              placeholder="如: ¥6,999"
              className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500"
            />
            {parsePriceValue(form.price) > 0 && (
              <p className="text-xs text-slate-500 mt-1">
                数值: {parsePriceValue(form.price)}
              </p>
            )}
          </div>

          {/* 颜色 & 色值 */}
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block text-xs text-slate-500 uppercase mb-1">颜色</label>
              <input
                value={form.color}
                onChange={(e) => updateField('color', e.target.value)}
                placeholder="如: 曜金黑"
                className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs text-slate-500 uppercase mb-1">色值</label>
              <div className="flex items-center gap-2">
                <input
                  value={form.colorHex}
                  onChange={(e) => updateField('colorHex', e.target.value)}
                  placeholder="如: #1a1a2e"
                  className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500 font-mono"
                />
                {form.colorHex && /^#[0-9a-fA-F]{6}$/.test(form.colorHex) && (
                  <span
                    className="inline-block w-6 h-6 rounded-full border border-slate-600 flex-shrink-0"
                    style={{ backgroundColor: form.colorHex }}
                  />
                )}
              </div>
            </div>
          </div>

          {/* 规格 JSON */}
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">规格</label>
            <textarea
              value={form.specJson}
              onChange={(e) => updateField('specJson', e.target.value)}
              placeholder={`[{"key":"芯片","val":"9000S"},{"key":"内存","val":"12GB"}]`}
              rows={3}
              className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm font-mono focus:outline-none focus:border-blue-500 resize-y"
            />
            <p className="text-xs text-slate-500 mt-1">
              JSON 数组格式，每项包含 key/val 字段
            </p>
          </div>

          {/* 图像路径 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">图片路径</label>
            <input
              value={form.imagePath}
              onChange={(e) => updateField('imagePath', e.target.value)}
              placeholder="如: products/huawei-mate70.png"
              className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500 font-mono"
            />
          </div>

          {/* 备注 */}
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">备注</label>
            <textarea
              value={form.notes}
              onChange={(e) => updateField('notes', e.target.value)}
              placeholder="补充说明..."
              rows={2}
              className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 w-full text-sm focus:outline-none focus:border-blue-500 resize-y"
            />
          </div>

          {/* 错误 & 按钮 */}
          {submitError && (
            <p className="text-red-400 text-xs">{submitError}</p>
          )}
          <div className="flex justify-end gap-3 pt-2">
            <button
              onClick={closeModal}
              className="px-4 py-2 text-sm text-slate-400 hover:text-slate-200 hover:bg-slate-800 rounded-lg transition-colors"
            >
              取消
            </button>
            <button
              onClick={handleSubmit}
              disabled={submitting}
              className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium transition-colors disabled:opacity-50"
            >
              {submitting ? '提交中...' : editing ? '保存修改' : '添加产品'}
            </button>
          </div>
        </div>
      </Modal>

      {/* ===== 删除确认 ===== */}
      <ConfirmDialog
        open={!!deleteTarget}
        onClose={() => setDeleteTarget(null)}
        onConfirm={handleDelete}
        title="删除产品"
        message={`确定要删除「${deleteTarget?.brand} ${deleteTarget?.model}」吗？此操作不可撤销。`}
        confirmLabel="删除"
        danger
      />
    </div>
  );
}
