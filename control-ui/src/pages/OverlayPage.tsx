// ============================================================
// 叠加层编辑 — obs-multicam-review
// 时间码 + 产品卡片 + 排行榜叠加配置
// ============================================================
import { useState, useCallback } from 'react';
import {
  Monitor, Eye, EyeOff, ExternalLink, Save, Loader2,
  AlertTriangle, Clock, Tag, Trophy,
} from 'lucide-react';

// ── 类型 ──────────────────────────────────────────────

type Position = 'top-left' | 'top-right' | 'bottom-left' | 'bottom-right';

interface TimecodeConfig {
  enabled: boolean;
  position: Position;
  fontSize: number;
  fontColor: string;
  bgOpacity: number;
  fps: number;
}

interface ProductCardConfig {
  enabled: boolean;
  productId: string;
  position: Position;
  animation: 'none' | 'fade' | 'slide-left' | 'slide-up';
}

interface LeaderboardConfig {
  enabled: boolean;
  maxItems: number;
  animation: 'none' | 'fade' | 'slide-up';
}

interface OverlayConfig {
  timecode: TimecodeConfig;
  productCard: ProductCardConfig;
  leaderboard: LeaderboardConfig;
}

// ── 常量 ──────────────────────────────────────────────

const POSITION_LABELS: Record<Position, string> = {
  'top-left': '左上',
  'top-right': '右上',
  'bottom-left': '左下',
  'bottom-right': '右下',
};

const ANIMATION_LABELS: Record<string, string> = {
  none: '无',
  fade: '淡入',
  'slide-left': '左滑',
  'slide-up': '上滑',
};

const DEFAULT_CONFIG: OverlayConfig = {
  timecode: {
    enabled: true,
    position: 'bottom-right',
    fontSize: 24,
    fontColor: '#ffffff',
    bgOpacity: 0.5,
    fps: 30,
  },
  productCard: {
    enabled: false,
    productId: '',
    position: 'bottom-left',
    animation: 'fade',
  },
  leaderboard: {
    enabled: false,
    maxItems: 5,
    animation: 'slide-up',
  },
};

interface Product {
  id: string;
  brand: string;
  model: string;
}

const MOCK_PRODUCTS: Product[] = [
  { id: 'p1', brand: '品牌A', model: 'XYZ-100' },
  { id: 'p2', brand: '品牌B', model: 'ABC-200' },
  { id: 'p3', brand: '品牌C', model: 'QWE-300' },
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

// ── 区块头 ────────────────────────────────────────────

function SectionHeader({
  icon: Icon,
  title,
  enabled,
  onToggle,
}: {
  icon: React.ComponentType<{ size?: number | string; className?: string }>;
  title: string;
  enabled: boolean;
  onToggle: () => void;
}) {
  return (
    <div className="flex items-center justify-between mb-3">
      <h3 className="text-sm font-semibold text-slate-200 flex items-center gap-2">
        <Icon size={16} className="text-slate-400" />
        {title}
      </h3>
      <button
        onClick={onToggle}
        className={`flex items-center gap-1.5 text-xs px-3 py-1.5 rounded-lg border transition-all ${
          enabled
            ? 'bg-green-600/20 border-green-500/40 text-green-300'
            : 'border-slate-700 text-slate-500 hover:text-slate-400'
        }`}
      >
        {enabled ? <Eye size={13} /> : <EyeOff size={13} />}
        {enabled ? '显示中' : '已隐藏'}
      </button>
    </div>
  );
}

// ── 输入字段 ──────────────────────────────────────────

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <div className="text-[10px] text-slate-500 uppercase tracking-wide mb-1">{label}</div>
      {children}
    </div>
  );
}

// ── 时间码配置 ────────────────────────────────────────

function TimecodeSection({
  config,
  onChange,
}: {
  config: TimecodeConfig;
  onChange: (patch: Partial<TimecodeConfig>) => void;
}) {
  return (
    <div className="bg-slate-800/40 backdrop-blur-sm border border-slate-700/50 rounded-xl px-4 py-3.5 space-y-3">
      <SectionHeader
        icon={Clock}
        title="时间码叠加"
        enabled={config.enabled}
        onToggle={() => onChange({ enabled: !config.enabled })}
      />

      <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
        <Field label="位置">
          <select
            value={config.position}
            onChange={e => onChange({ position: e.target.value as Position })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {Object.entries(POSITION_LABELS).map(([value, label]) => (
              <option key={value} value={value}>{label}</option>
            ))}
          </select>
        </Field>

        <Field label="字号">
          <input
            type="number" min={12} max={72}
            value={config.fontSize}
            onChange={e => onChange({ fontSize: Number(e.target.value) })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors"
          />
        </Field>

        <Field label="颜色">
          <input
            type="color"
            value={config.fontColor}
            onChange={e => onChange({ fontColor: e.target.value })}
            className="w-full h-9 bg-slate-900 border border-slate-600 rounded-lg p-0.5
                       cursor-pointer"
          />
        </Field>

        <Field label="背景透明度">
          <input
            type="range" min={0} max={100} value={Math.round(config.bgOpacity * 100)}
            onChange={e => onChange({ bgOpacity: Number(e.target.value) / 100 })}
            className="w-full h-1 bg-slate-700 rounded-lg appearance-none cursor-pointer
                       [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-3
                       [&::-webkit-slider-thumb]:h-3 [&::-webkit-slider-thumb]:bg-blue-400
                       [&::-webkit-slider-thumb]:rounded-full"
          />
          <span className="text-[10px] text-slate-500">{Math.round(config.bgOpacity * 100)}%</span>
        </Field>

        <Field label="帧率">
          <select
            value={config.fps}
            onChange={e => onChange({ fps: Number(e.target.value) })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {[24, 25, 30, 50, 60].map(f => (
              <option key={f} value={f}>{f} fps</option>
            ))}
          </select>
        </Field>
      </div>
    </div>
  );
}

// ── 产品卡片配置 ──────────────────────────────────────

function ProductCardSection({
  config,
  onChange,
}: {
  config: ProductCardConfig;
  onChange: (patch: Partial<ProductCardConfig>) => void;
}) {
  return (
    <div className="bg-slate-800/40 backdrop-blur-sm border border-slate-700/50 rounded-xl px-4 py-3.5 space-y-3">
      <SectionHeader
        icon={Tag}
        title="产品卡片叠加"
        enabled={config.enabled}
        onToggle={() => onChange({ enabled: !config.enabled })}
      />

      <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
        <Field label="产品选择">
          <select
            value={config.productId}
            onChange={e => onChange({ productId: e.target.value })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            <option value="">-- 选择产品 --</option>
            {MOCK_PRODUCTS.map(p => (
              <option key={p.id} value={p.id}>{p.brand} {p.model}</option>
            ))}
          </select>
        </Field>

        <Field label="显示位置">
          <select
            value={config.position}
            onChange={e => onChange({ position: e.target.value as Position })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {Object.entries(POSITION_LABELS).map(([value, label]) => (
              <option key={value} value={value}>{label}</option>
            ))}
          </select>
        </Field>

        <Field label="动画效果">
          <select
            value={config.animation}
            onChange={e => onChange({ animation: e.target.value as ProductCardConfig['animation'] })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {Object.entries(ANIMATION_LABELS).map(([value, label]) => (
              <option key={value} value={value}>{label}</option>
            ))}
          </select>
        </Field>
      </div>
    </div>
  );
}

// ── 排行榜配置 ────────────────────────────────────────

function LeaderboardSection({
  config,
  onChange,
}: {
  config: LeaderboardConfig;
  onChange: (patch: Partial<LeaderboardConfig>) => void;
}) {
  return (
    <div className="bg-slate-800/40 backdrop-blur-sm border border-slate-700/50 rounded-xl px-4 py-3.5 space-y-3">
      <SectionHeader
        icon={Trophy}
        title="排行榜叠加"
        enabled={config.enabled}
        onToggle={() => onChange({ enabled: !config.enabled })}
      />

      <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
        <Field label="最大显示数量">
          <input
            type="number" min={3} max={20}
            value={config.maxItems}
            onChange={e => onChange({ maxItems: Number(e.target.value) })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors"
          />
        </Field>

        <Field label="更新动画">
          <select
            value={config.animation}
            onChange={e => onChange({ animation: e.target.value as LeaderboardConfig['animation'] })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {Object.entries(ANIMATION_LABELS)
              .filter(([k]) => k !== 'slide-left')
              .map(([value, label]) => (
                <option key={value} value={value}>{label}</option>
              ))}
          </select>
        </Field>
      </div>
    </div>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function OverlayPage() {
  const [config, setConfig] = useState<OverlayConfig>(DEFAULT_CONFIG);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // ── 预览 ──
  const handlePreview = useCallback(() => {
    window.open('/overlay/timecode.html', '_blank', 'width=960,height=540');
  }, []);

  // ── 保存 ──
  const handleSave = useCallback(async () => {
    setSaving(true);
    try {
      // await api request POST /api/overlay/config
      await new Promise(r => setTimeout(r, 400));
    } catch (err) {
      alert('保存失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setSaving(false);
    }
  }, []);

  if (error) return <ErrorState error={error} />;

  return (
    <div className="max-w-3xl mx-auto space-y-5">
      {/* 页头 */}
      <div className="flex items-center justify-between">
        <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
          <Monitor size={20} className="text-cyan-400" />
          叠加层编辑
        </h2>
        <div className="flex items-center gap-2">
          <button
            onClick={handlePreview}
            className="flex items-center gap-1.5 px-3 py-2 text-xs text-slate-300
                       bg-slate-700 hover:bg-slate-600 rounded-lg transition-colors"
          >
            <ExternalLink size={14} />
            预览
          </button>
          <button
            onClick={handleSave}
            disabled={saving}
            className="flex items-center gap-1.5 px-4 py-2 text-xs font-medium text-white
                       bg-blue-600 hover:bg-blue-700 rounded-lg transition-colors
                       disabled:opacity-50"
          >
            {saving ? <Loader2 size={14} className="animate-spin" /> : <Save size={14} />}
            保存设置
          </button>
        </div>
      </div>

      {/* 时间码 */}
      <TimecodeSection
        config={config.timecode}
        onChange={patch => setConfig(prev => ({ ...prev, timecode: { ...prev.timecode, ...patch } }))}
      />

      {/* 产品卡片 */}
      <ProductCardSection
        config={config.productCard}
        onChange={patch => setConfig(prev => ({ ...prev, productCard: { ...prev.productCard, ...patch } }))}
      />

      {/* 排行榜 */}
      <LeaderboardSection
        config={config.leaderboard}
        onChange={patch => setConfig(prev => ({ ...prev, leaderboard: { ...prev.leaderboard, ...patch } }))}
      />
    </div>
  );
}
