// ============================================================
// 评分会话页面 — obs-multicam-review
// 项目选择 → 会话管理 → 打分矩阵 → 排行榜
// ============================================================
import { useState, useEffect, useCallback, useRef } from 'react';
import {
  Plus, Trophy, CheckCircle, Download,
  Loader2, AlertTriangle, Star, User, Clock,
  Hash, Lock,
} from 'lucide-react';
import { useApi } from '../hooks/useApi';
import * as api from '../services/api-client';
import { Modal } from '../components/Modal';
import { ConfirmDialog } from '../components/ConfirmDialog';
import type {
  Project, ScoringSession, LeaderboardEntry,
  DimensionItem, Product,
} from '../types/api';

// ============ 会话创建表单 ============
interface SessionForm {
  judgeName: string;
  sessionName: string;
}

// ============ 产品维度映射 ============
interface ProductDimensions {
  productId: string;
  items: DimensionItem[];
  templateName: string;
}

// ============ 每产品每维度的分数缓存 ============
type ScoreMap = Record<string, Record<string, number>>; // productId → dimKey → score

// ============ 会话状态标签映射 ============
const SESSION_STATUS_MAP: Record<string, { label: string; cls: string }> = {
  in_progress: { label: '进行中', cls: 'bg-blue-900/50 text-blue-300 border border-blue-700' },
  completed:  { label: '已完成', cls: 'bg-green-900/50 text-green-300 border border-green-700' },
};

// ============ 页面组件 ============
export default function ScoringPage() {
  // --- 项目相关 ---
  const { data: projectData, loading: projectsLoading } = useApi(api.fetchProjectList);
  const [projectId, setProjectId] = useState('');
  const projects = projectData?.projects ?? [];

  // --- 会话相关 ---
  const [sessions, setSessions] = useState<ScoringSession[]>([]);
  const [sessionsLoading, setSessionsLoading] = useState(false);
  const [sessionId, setSessionId] = useState('');
  const [session, setSession] = useState<ScoringSession | null>(null);

  // --- 创建会话表单 ---
  const [showCreateSession, setShowCreateSession] = useState(false);
  const [sessionForm, setSessionForm] = useState<SessionForm>({ judgeName: '', sessionName: '' });
  const [creating, setCreating] = useState(false);
  const [createError, setCreateError] = useState('');

  // --- 打分数据 ---
  const [products, setProducts] = useState<Product[]>([]);
  const [prodDimensions, setProdDimensions] = useState<ProductDimensions[]>([]);
  const [scores, setScores] = useState<ScoreMap>({});
  const [leaderboard, setLeaderboard] = useState<LeaderboardEntry[]>([]);
  const [dataLoading, setDataLoading] = useState(false);
  const [dataError, setDataError] = useState('');

  // --- 完成会话 ---
  const [completing, setCompleting] = useState(false);
  const [showCompleteConfirm, setShowCompleteConfirm] = useState(false);

  // --- 分数保存防抖定时器 ---
  const saveTimers = useRef<Record<string, ReturnType<typeof setTimeout>>>({});

  const isCompleted = session?.status === 'completed';

  // ========== 项目切换：重新加载会话 ==========
  useEffect(() => {
    if (!projectId) {
      setSessions([]);
      setSessionId('');
      return;
    }
    setSessionsLoading(true);
    api.fetchScoringSessions(projectId)
      .then((res) => setSessions(res.sessions ?? []))
      .catch(() => setSessions([]))
      .finally(() => setSessionsLoading(false));
  }, [projectId]);

  // ========== 会话切换：加载数据 ==========
  useEffect(() => {
    if (!sessionId) {
      setSession(null);
      setProducts([]);
      setProdDimensions([]);
      setScores({});
      setLeaderboard([]);
      return;
    }

    // 加载会话详情
    api.fetchScoringSession(sessionId)
      .then(setSession)
      .catch(() => setSession(null));

    loadSessionData(sessionId);
  }, [sessionId]);

  async function loadSessionData(sid: string) {
    setDataLoading(true);
    setDataError('');
    try {
      // 加载会话详情获取 projectId
      const sess = await api.fetchScoringSession(sid);
      setSession(sess);

      // 加载产品列表
      const productRes = await api.fetchProductList(sess.projectId);
      const prods = productRes.products;
      setProducts(prods);

      // 加载每个产品的维度模板
      const dimResults: ProductDimensions[] = [];
      for (const p of prods) {
        try {
          const tmpl = await api.fetchProductDimTemplate(p.id);
          dimResults.push({
            productId: p.id,
            items: tmpl.items ?? [],
            templateName: tmpl.name,
          });
        } catch {
          dimResults.push({ productId: p.id, items: [], templateName: '' });
        }
      }
      setProdDimensions(dimResults);

      // 加载已有分数
      const scoreMap: ScoreMap = {};
      for (const p of prods) {
        try {
          const entries = await api.fetchScores(sid, p.id);
          if (entries.length > 0) {
            scoreMap[p.id] = {};
            for (const e of entries) {
              scoreMap[p.id][e.dimKey] = e.score;
            }
          }
        } catch {
          // 无分数则留空
        }
      }
      setScores(scoreMap);

      // 加载排行榜
      try {
        const lb = await api.fetchLeaderboard(sid);
        setLeaderboard(lb.leaderboard ?? []);
      } catch {
        setLeaderboard([]);
      }
    } catch (e) {
      setDataError(e instanceof Error ? e.message : '加载数据失败');
    } finally {
      setDataLoading(false);
    }
  }

  // ========== 创建会话 ==========
  async function handleCreateSession() {
    if (!sessionForm.judgeName.trim() || !sessionForm.sessionName.trim()) {
      setCreateError('评委名称和会话名称为必填项');
      return;
    }
    setCreating(true);
    setCreateError('');
    try {
      const created = await api.createScoringSession({
        projectId,
        judgeName: sessionForm.judgeName.trim(),
        sessionName: sessionForm.sessionName.trim(),
      });
      setShowCreateSession(false);
      setSessionForm({ judgeName: '', sessionName: '' });
      // 刷新会话列表
      const res = await api.fetchScoringSessions(projectId);
      setSessions(res.sessions ?? []);
      setSessionId(created.id);
    } catch (e) {
      setCreateError(e instanceof Error ? e.message : '创建失败');
    } finally {
      setCreating(false);
    }
  }

  // ========== 分数变更（带防抖保存） ==========
  function handleScoreChange(productId: string, dimKey: string, value: string) {
    if (isCompleted) return;

    const numValue = value === '' ? NaN : Math.max(0, Math.min(10, parseFloat(value) || 0));

    // 立即更新本地状态（响应式 UI）
    setScores((prev) => {
      const next = { ...prev };
      if (!next[productId]) next[productId] = {};
      if (isNaN(numValue)) {
        delete next[productId][dimKey];
      } else {
        next[productId][dimKey] = numValue;
      }
      return next;
    });

    // 清除已有的防抖定时器
    const timerKey = `${productId}:${dimKey}`;
    if (saveTimers.current[timerKey]) {
      clearTimeout(saveTimers.current[timerKey]);
    }

    // 空值不清除已有分数（用户可能在编辑中），等失去焦点时再处理
    if (isNaN(numValue)) return;

    // 300ms 防抖后提交
    saveTimers.current[timerKey] = setTimeout(async () => {
      try {
        await api.submitScore({
          sessionId,
          productId,
          dimKey,
          score: numValue,
        });
        // 提交成功后刷新排行榜
        try {
          const lb = await api.fetchLeaderboard(sessionId);
          setLeaderboard(lb.leaderboard ?? []);
        } catch { /* ignore */ }
      } catch {
        // 静默失败 — 分数仍保留在本地状态
      }
    }, 300);
  }

  // ========== 失去焦点时提交空白值（删除分数） ==========
  function handleScoreBlur(productId: string, dimKey: string) {
    const currentScore = scores[productId]?.[dimKey];
    if (currentScore === undefined) return;
    // 分数已通过防抖保存，无需额外操作
  }

  // ========== 完成会话 ==========
  async function handleComplete() {
    setCompleting(true);
    try {
      const updated = await api.completeScoringSession(sessionId);
      setSession(updated);
      setShowCompleteConfirm(false);
    } catch (e) {
      setDataError(e instanceof Error ? e.message : '完成会话失败');
    } finally {
      setCompleting(false);
    }
  }

  // ========== 导出 CSV ==========
  function handleExport() {
    if (leaderboard.length === 0) return;

    // 收集所有维度键（去重且保持顺序）
    const dimKeys = new Set<string>();
    const dimLabels: Record<string, string> = {};
    for (const entry of leaderboard) {
      for (const d of entry.dimensions) {
        if (!dimKeys.has(d.dimKey)) {
          dimKeys.add(d.dimKey);
          dimLabels[d.dimKey] = d.label;
        }
      }
    }
    const dimKeyList = Array.from(dimKeys);

    // 构建 CSV 头
    const headers = ['排名', '品牌', '型号', '总分', ...dimKeyList.map((k) => dimLabels[k] || k)];
    const rows = leaderboard.map((entry, i) => {
      const dimScores = dimKeyList.map((k) => {
        const found = entry.dimensions.find((d) => d.dimKey === k);
        return found ? `${found.score}/${found.maxScore}` : '-';
      });
      return [
        String(i + 1),
        entry.brand,
        entry.model,
        `${entry.totalScore}/${entry.maxPossible}`,
        ...dimScores,
      ];
    });

    // 生成 CSV 内容（BOM for Excel 中文兼容）
    const bom = '\uFEFF';
    const csv = bom + [
      headers.join(','),
      ...rows.map((r) => r.map((c) => `"${c}"`).join(',')),
    ].join('\n');

    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `评分排行榜_${session?.sessionName ?? sessionId}_${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }

  // ========== 获取统一维度列表（所有产品维度的并集） ==========
  const allDimKeys: string[] = [];
  const dimMeta: Record<string, { label: string; maxScore: number; weight: number }> = {};
  for (const pd of prodDimensions) {
    for (const item of pd.items) {
      if (!dimMeta[item.dimKey]) {
        dimMeta[item.dimKey] = {
          label: item.label || item.dimKey,
          maxScore: item.maxScore ?? 10,
          weight: item.weight ?? 1,
        };
        allDimKeys.push(item.dimKey);
      }
    }
  }

  // ========== 获取产品维度的所有维度键 ==========
  const productDimKeys = useCallback((productId: string): string[] => {
    const pd = prodDimensions.find((d) => d.productId === productId);
    return pd?.items.map((i) => i.dimKey) ?? [];
  }, [prodDimensions]);

  // ========== 渲染 ==========

  // 项目加载中
  if (projectsLoading) {
    return (
      <div className="flex items-center justify-center h-full">
        <Loader2 size={32} className="animate-spin text-slate-500" />
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* ===== 顶部控制栏 ===== */}
      <div className="flex flex-wrap items-center gap-3">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <Star size={20} className="text-yellow-400" />
          产品评分
        </h2>

        {/* 项目选择器 */}
        <div className="flex items-center gap-2">
          <span className="text-xs text-slate-500">项目:</span>
          <select
            value={projectId}
            onChange={(e) => { setProjectId(e.target.value); setSessionId(''); }}
            className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-1.5 text-slate-200 text-sm focus:outline-none focus:border-blue-500"
          >
            <option value="">-- 选择项目 --</option>
            {projects.map((p) => (
              <option key={p.id} value={p.id}>{p.name}</option>
            ))}
          </select>
        </div>

        {/* 会话选择器 */}
        {projectId && (
          <div className="flex items-center gap-2">
            <span className="text-xs text-slate-500">会话:</span>
            {sessionsLoading ? (
              <Loader2 size={14} className="animate-spin text-slate-500" />
            ) : (
              <>
                <select
                  value={sessionId}
                  onChange={(e) => setSessionId(e.target.value)}
                  className="bg-slate-800 border border-slate-600 rounded-lg px-3 py-1.5 text-slate-200 text-sm focus:outline-none focus:border-blue-500 min-w-[160px]"
                >
                  <option value="">-- 选择会话 --</option>
                  {sessions.map((s) => (
                    <option key={s.id} value={s.id}>
                      {s.sessionName} ({s.judgeName})
                    </option>
                  ))}
                </select>
                <button
                  onClick={() => {
                    setSessionForm({ judgeName: '', sessionName: '' });
                    setCreateError('');
                    setShowCreateSession(true);
                  }}
                  className="flex items-center gap-1 px-3 py-1.5 bg-blue-600 hover:bg-blue-700 text-white text-xs font-medium rounded-lg transition-colors"
                >
                  <Plus size={14} /> 新建
                </button>
              </>
            )}
          </div>
        )}
      </div>

      {/* ===== 空状态：未选项目 ===== */}
      {!projectId && (
        <div className="flex flex-col items-center justify-center py-20 text-slate-500">
          <Star size={48} className="mb-3 opacity-30" />
          <p className="text-sm">请先选择一个项目</p>
        </div>
      )}

      {/* ===== 空状态：已选项目但未选会话 ===== */}
      {projectId && !sessionId && !sessionsLoading && (
        <div className="flex flex-col items-center justify-center py-20 text-slate-500">
          <Trophy size={48} className="mb-3 opacity-30" />
          <p className="text-sm">
            {sessions.length === 0 ? '暂无评分会话' : '请选择一个评分会话'}
          </p>
          {sessions.length === 0 && (
            <button
              onClick={() => setShowCreateSession(true)}
              className="mt-3 flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors"
            >
              <Plus size={16} /> 创建第一个评分会话
            </button>
          )}
        </div>
      )}

      {/* ===== 会话详情区块 ===== */}
      {sessionId && session && (
        <>
          {/* 会话信息栏 */}
          <div className="bg-slate-800 border border-slate-700 rounded-lg px-4 py-3">
            <div className="flex flex-wrap items-center justify-between gap-3">
              <div className="flex items-center gap-4">
                <h3 className="text-slate-100 font-semibold text-sm">{session.sessionName}</h3>
                <span className={`text-xs px-2 py-0.5 rounded-full ${SESSION_STATUS_MAP[session.status]?.cls ?? ''}`}>
                  {SESSION_STATUS_MAP[session.status]?.label ?? session.status}
                </span>
                {isCompleted && <Lock size={14} className="text-slate-500" />}
              </div>
              <div className="flex items-center gap-4 text-xs text-slate-400">
                <span className="flex items-center gap-1">
                  <User size={12} /> {session.judgeName}
                </span>
                <span className="flex items-center gap-1">
                  <Clock size={12} /> {new Date(session.startedAt).toLocaleString('zh-CN')}
                </span>
                {session.completedAt && (
                  <span className="text-green-400 flex items-center gap-1">
                    <CheckCircle size={12} /> 完成于 {new Date(session.completedAt).toLocaleString('zh-CN')}
                  </span>
                )}
              </div>
              {/* 操作按钮 */}
              <div className="flex items-center gap-2">
                {!isCompleted && (
                  <button
                    onClick={() => setShowCompleteConfirm(true)}
                    disabled={completing}
                    className="flex items-center gap-1 px-3 py-1.5 bg-green-600 hover:bg-green-700 text-white text-xs font-medium rounded-lg transition-colors disabled:opacity-50"
                  >
                    {completing ? (
                      <Loader2 size={12} className="animate-spin" />
                    ) : (
                      <CheckCircle size={12} />
                    )}
                    完成会话
                  </button>
                )}
                <button
                  onClick={handleExport}
                  disabled={leaderboard.length === 0}
                  className="flex items-center gap-1 px-3 py-1.5 bg-slate-700 hover:bg-slate-600 text-slate-200 text-xs font-medium rounded-lg transition-colors disabled:opacity-40"
                >
                  <Download size={12} />
                  导出 CSV
                </button>
              </div>
            </div>
          </div>

          {/* 数据加载态 & 错误态 */}
          {dataLoading && (
            <div className="flex items-center justify-center py-16">
              <Loader2 size={28} className="animate-spin text-slate-500" />
            </div>
          )}
          {dataError && (
            <div className="flex flex-col items-center justify-center py-12 gap-3">
              <AlertTriangle size={36} className="text-red-400" />
              <p className="text-red-400 text-sm">{dataError}</p>
              <button
                onClick={() => loadSessionData(sessionId)}
                className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm rounded-lg"
              >
                重试
              </button>
            </div>
          )}

          {!dataLoading && !dataError && (
            <>
              {/* ===== 打分矩阵 ===== */}
              <ScoringGrid
                products={products}
                allDimKeys={allDimKeys}
                dimMeta={dimMeta}
                productDimKeys={productDimKeys}
                scores={scores}
                isCompleted={isCompleted}
                onScoreChange={handleScoreChange}
                onScoreBlur={handleScoreBlur}
              />

              {/* ===== 排行榜 ===== */}
              <LeaderboardTable
                leaderboard={leaderboard}
                allDimKeys={allDimKeys}
                dimMeta={dimMeta}
              />
            </>
          )}
        </>
      )}

      {/* ===== 创建会话模态框 ===== */}
      <Modal open={showCreateSession} onClose={() => setShowCreateSession(false)} title="新建评分会话" width="sm">
        <div className="space-y-3">
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">
              评委名称 <span className="text-red-400">*</span>
            </label>
            <input
              value={sessionForm.judgeName}
              onChange={(e) => setSessionForm({ ...sessionForm, judgeName: e.target.value })}
              placeholder="如：张三"
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm focus:outline-none focus:border-blue-500"
              autoFocus
            />
          </div>
          <div>
            <label className="block text-xs text-slate-500 uppercase mb-1">
              会话名称 <span className="text-red-400">*</span>
            </label>
            <input
              value={sessionForm.sessionName}
              onChange={(e) => setSessionForm({ ...sessionForm, sessionName: e.target.value })}
              placeholder="如：第一轮评分"
              className="w-full bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-slate-200 text-sm focus:outline-none focus:border-blue-500"
            />
          </div>
          {createError && <p className="text-red-400 text-xs">{createError}</p>}
          <div className="flex justify-end gap-3 pt-2">
            <button
              onClick={() => setShowCreateSession(false)}
              className="px-4 py-2 text-sm text-slate-400 hover:text-slate-200 hover:bg-slate-800 rounded-lg transition-colors"
            >
              取消
            </button>
            <button
              onClick={handleCreateSession}
              disabled={creating}
              className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white text-sm font-medium rounded-lg transition-colors disabled:opacity-50"
            >
              {creating ? '创建中...' : '创建会话'}
            </button>
          </div>
        </div>
      </Modal>

      {/* ===== 完成会话确认 ===== */}
      <ConfirmDialog
        open={showCompleteConfirm}
        onClose={() => setShowCompleteConfirm(false)}
        onConfirm={handleComplete}
        title="完成评分会话"
        message="确定要完成此评分会话吗？完成后所有分数将被锁定，无法再修改。"
        confirmLabel="确认完成"
        danger={false}
      />
    </div>
  );
}

// ================================================================
// 打分矩阵子组件
// ================================================================
function ScoringGrid({
  products,
  allDimKeys,
  dimMeta,
  productDimKeys,
  scores,
  isCompleted,
  onScoreChange,
  onScoreBlur,
}: {
  products: Product[];
  allDimKeys: string[];
  dimMeta: Record<string, { label: string; maxScore: number; weight: number }>;
  productDimKeys: (productId: string) => string[];
  scores: ScoreMap;
  isCompleted: boolean;
  onScoreChange: (productId: string, dimKey: string, value: string) => void;
  onScoreBlur: (productId: string, dimKey: string) => void;
}) {
  if (products.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-12 text-slate-500">
        <Hash size={36} className="mb-2 opacity-30" />
        <p className="text-sm">该项目暂无产品，请先在项目管理中添加产品</p>
      </div>
    );
  }

  if (allDimKeys.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-12 text-slate-500 bg-slate-800/50 border border-slate-700 rounded-lg">
        <AlertTriangle size={36} className="mb-2 text-yellow-500/50" />
        <p className="text-sm">产品尚未绑定评分维度模板</p>
        <p className="text-xs mt-1 text-slate-600">
          请先在「维度模板」页面创建模板，并在产品管理中绑定
        </p>
      </div>
    );
  }

  return (
    <div className="bg-slate-800/50 border border-slate-700 rounded-lg overflow-hidden">
      <div className="px-4 py-2.5 border-b border-slate-700 flex items-center gap-2">
        <Hash size={14} className="text-blue-400" />
        <h3 className="text-sm font-semibold text-slate-200">打分矩阵</h3>
        {isCompleted && (
          <span className="text-xs text-slate-500 ml-2">（已锁定）</span>
        )}
      </div>
      <div className="overflow-x-auto">
        <table className="w-full text-xs">
          <thead>
            <tr className="border-b border-slate-700">
              <th className="sticky left-0 bg-slate-800 text-left px-4 py-2 text-slate-400 font-medium min-w-[140px]">
                产品
              </th>
              {allDimKeys.map((dk) => (
                <th key={dk} className="px-3 py-2 text-slate-400 font-medium text-center min-w-[80px]">
                  <div>{dimMeta[dk]?.label ?? dk}</div>
                  <div className="text-[10px] text-slate-600 font-normal">
                    0-{dimMeta[dk]?.maxScore ?? 10}
                    {dimMeta[dk]?.weight !== 1 && (
                      <span className="ml-1">×{dimMeta[dk]?.weight}</span>
                    )}
                  </div>
                </th>
              ))}
              <th className="px-4 py-2 text-slate-400 font-medium text-center min-w-[60px]">
                小计
              </th>
            </tr>
          </thead>
          <tbody>
            {products.map((product) => {
              const pKeys = productDimKeys(product.id);
              const hasDimensions = pKeys.length > 0;

              // 计算小计
              let subtotal = 0;
              let subtotalMax = 0;
              for (const dk of pKeys) {
                const meta = dimMeta[dk];
                if (!meta) continue;
                const s = scores[product.id]?.[dk];
                if (s !== undefined && !isNaN(s)) {
                  subtotal += s * meta.weight;
                }
                subtotalMax += meta.maxScore * meta.weight;
              }

              return (
                <tr
                  key={product.id}
                  className="border-b border-slate-700/50 hover:bg-slate-800/80 transition-colors"
                >
                  <td className="sticky left-0 bg-slate-800 px-4 py-2.5">
                    <div className="text-slate-200 font-medium truncate max-w-[150px]">
                      {product.brand}
                    </div>
                    <div className="text-slate-500 text-[11px] truncate max-w-[150px]">
                      {product.model}
                    </div>
                    {!hasDimensions && (
                      <span className="text-[10px] text-yellow-500">未绑定维度</span>
                    )}
                  </td>

                  {allDimKeys.map((dk) => {
                    const hasDim = pKeys.includes(dk);
                    const meta = dimMeta[dk];
                    const val = scores[product.id]?.[dk];

                    if (!hasDim) {
                      return (
                        <td key={dk} className="px-3 py-2.5 text-center">
                          <span className="text-slate-700">—</span>
                        </td>
                      );
                    }

                    return (
                      <td key={dk} className="px-3 py-2.5 text-center">
                        <input
                          type="number"
                          min={0}
                          max={meta?.maxScore ?? 10}
                          step="0.1"
                          value={val !== undefined && !isNaN(val) ? val : ''}
                          onChange={(e) => onScoreChange(product.id, dk, e.target.value)}
                          onBlur={() => onScoreBlur(product.id, dk)}
                          disabled={isCompleted}
                          placeholder="-"
                          className={`w-16 text-center bg-slate-900 border border-slate-600 rounded px-2 py-1.5 text-slate-200 text-xs
                            focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500/30
                            disabled:opacity-60 disabled:cursor-not-allowed
                            ${val !== undefined && !isNaN(val) ? 'text-yellow-300 font-semibold' : ''}`}
                        />
                      </td>
                    );
                  })}

                  <td className="px-4 py-2.5 text-center">
                    <span className={`text-xs font-semibold ${subtotal > 0 ? 'text-yellow-300' : 'text-slate-600'}`}>
                      {subtotalMax > 0 ? `${subtotal.toFixed(1)}/${subtotalMax}` : '-'}
                    </span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}

// ================================================================
// 排行榜子组件
// ================================================================
function LeaderboardTable({
  leaderboard,
  allDimKeys,
  dimMeta,
}: {
  leaderboard: LeaderboardEntry[];
  allDimKeys: string[];
  dimMeta: Record<string, { label: string; maxScore: number; weight: number }>;
}) {
  if (leaderboard.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-12 text-slate-500 bg-slate-800/50 border border-slate-700 rounded-lg">
        <Trophy size={36} className="mb-2 opacity-30" />
        <p className="text-sm">暂无排行数据，请先录入分数</p>
      </div>
    );
  }

  // 奖牌映射（前三名）
  const rankMedals: Record<number, string> = { 1: '🥇', 2: '🥈', 3: '🥉' };

  return (
    <div className="bg-slate-800/50 border border-slate-700 rounded-lg overflow-hidden">
      <div className="px-4 py-2.5 border-b border-slate-700 flex items-center gap-2">
        <Trophy size={14} className="text-yellow-400" />
        <h3 className="text-sm font-semibold text-slate-200">排行榜</h3>
      </div>
      <div className="overflow-x-auto">
        <table className="w-full text-xs">
          <thead>
            <tr className="border-b border-slate-700">
              <th className="px-3 py-2 text-slate-400 font-medium text-center w-12">#</th>
              <th className="text-left px-4 py-2 text-slate-400 font-medium">产品</th>
              {allDimKeys.map((dk) => (
                <th key={dk} className="px-3 py-2 text-slate-400 font-medium text-center min-w-[70px]">
                  {dimMeta[dk]?.label ?? dk}
                </th>
              ))}
              <th className="px-4 py-2 text-slate-400 font-medium text-center min-w-[80px]">总分</th>
            </tr>
          </thead>
          <tbody>
            {leaderboard.map((entry, idx) => {
              const rank = idx + 1;
              return (
                <tr
                  key={entry.productId}
                  className={`border-b border-slate-700/50 transition-colors
                    ${rank === 1 ? 'bg-yellow-900/10' : ''}
                    ${rank === 2 ? 'bg-slate-400/5' : ''}
                    ${rank === 3 ? 'bg-amber-900/10' : ''}
                  `}
                >
                  <td className="px-3 py-2.5 text-center">
                    <span className={rank <= 3 ? 'text-lg' : 'text-slate-500 font-medium'}>
                      {rankMedals[rank] ?? rank}
                    </span>
                  </td>
                  <td className="px-4 py-2.5">
                    <div className="text-slate-200 font-medium">{entry.brand}</div>
                    <div className="text-slate-500 text-[11px]">{entry.model}</div>
                  </td>
                  {allDimKeys.map((dk) => {
                    const dimScore = entry.dimensions.find((d) => d.dimKey === dk);
                    return (
                      <td key={dk} className="px-3 py-2.5 text-center">
                        {dimScore ? (
                          <span className={`font-semibold ${
                            dimScore.score === dimScore.maxScore
                              ? 'text-green-400'
                              : dimScore.score > 0
                                ? 'text-slate-300'
                                : 'text-slate-600'
                          }`}>
                            {dimScore.score}/{dimScore.maxScore}
                          </span>
                        ) : (
                          <span className="text-slate-700">—</span>
                        )}
                      </td>
                    );
                  })}
                  <td className="px-4 py-2.5 text-center">
                    <span className={`font-bold text-sm ${
                      rank === 1 ? 'text-yellow-300' : 'text-slate-200'
                    }`}>
                      {entry.totalScore}
                    </span>
                    <span className="text-slate-600 ml-0.5">/{entry.maxPossible}</span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}
