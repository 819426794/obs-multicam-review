// ============================================================
// 系统设置 — obs-multicam-review
// Web 服务器 / 录制 / 编码器 / 时间码 / 语言 / 关于
// ============================================================
import { useState, useCallback } from 'react';
import {
  Settings, Server, HardDrive, Film, Clock, Globe, Info,
  Loader2, AlertTriangle, Save, Monitor,
} from 'lucide-react';

// ── 类型 ──────────────────────────────────────────────

interface AppSettings {
  webPort: number;
  recordingPath: string;
  defaultEncoder: string;
  timecodeFps: number;
  language: string;
}

const ENCODER_OPTIONS = ['nvenc', 'qsv', 'x264'] as const;
const ENCODER_LABELS: Record<string, string> = {
  nvenc: 'NVENC (NVIDIA)',
  qsv: 'QSV (Intel)',
  x264: 'x264 (CPU)',
};

const LANGUAGE_OPTIONS = [
  { value: 'zh-CN', label: '简体中文' },
  { value: 'zh-TW', label: '繁體中文' },
  { value: 'en', label: 'English' },
  { value: 'ja', label: '日本語' },
];

const FPS_OPTIONS = [24, 25, 30, 50, 60];

const DEFAULT_SETTINGS: AppSettings = {
  webPort: 8080,
  recordingPath: 'C:\\Recordings',
  defaultEncoder: 'nvenc',
  timecodeFps: 30,
  language: 'zh-CN',
};

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

// ── 设置区块 ──────────────────────────────────────────

interface SectionProps {
  icon: React.ComponentType<{ size?: number; className?: string }>;
  title: string;
  children: React.ReactNode;
}

function Section({ icon: Icon, title, children }: SectionProps) {
  return (
    <div className="bg-slate-800/40 backdrop-blur-sm border border-slate-700/50 rounded-xl px-4 py-3.5">
      <h3 className="text-sm font-semibold text-slate-200 flex items-center gap-2 mb-3">
        <Icon size={16} className="text-slate-400" />
        {title}
      </h3>
      <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
        {children}
      </div>
    </div>
  );
}

function Field({
  label, hint, children,
}: {
  label: string; hint?: string; children: React.ReactNode;
}) {
  return (
    <div>
      <div className="text-[10px] text-slate-500 uppercase tracking-wide mb-1">{label}</div>
      {children}
      {hint && <div className="text-[10px] text-slate-600 mt-1">{hint}</div>}
    </div>
  );
}

// ── 关于区块 ──────────────────────────────────────────

function AboutSection() {
  return (
    <div className="bg-slate-800/40 backdrop-blur-sm border border-slate-700/50 rounded-xl px-4 py-3.5">
      <h3 className="text-sm font-semibold text-slate-200 flex items-center gap-2 mb-3">
        <Info size={16} className="text-slate-400" />
        关于
      </h3>

      <div className="space-y-2 text-sm">
        <div className="flex justify-between">
          <span className="text-slate-500">应用名称</span>
          <span className="text-slate-300">obs-multicam-review</span>
        </div>
        <div className="flex justify-between">
          <span className="text-slate-500">插件版本</span>
          <span className="text-slate-300 font-mono">v1.0.0</span>
        </div>
        <div className="flex justify-between">
          <span className="text-slate-500">控制面板版本</span>
          <span className="text-slate-300 font-mono">v1.0.0</span>
        </div>
        <div className="flex justify-between">
          <span className="text-slate-500">OBS WebSocket</span>
          <span className="text-slate-300 font-mono">obs-websocket 5.x</span>
        </div>
        <div className="flex justify-between">
          <span className="text-slate-500">技术栈</span>
          <span className="text-slate-300">React + TypeScript + Tailwind CSS</span>
        </div>
      </div>
    </div>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function SettingsPage() {
  const [settings, setSettings] = useState<AppSettings>(DEFAULT_SETTINGS);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);

  const update = useCallback((patch: Partial<AppSettings>) => {
    setSettings(prev => ({ ...prev, ...patch }));
    setSaved(false);
  }, []);

  // ── 保存 ──
  const handleSave = useCallback(async () => {
    setSaving(true);
    try {
      // await api POST /api/settings
      await new Promise(r => setTimeout(r, 500));
      setSaved(true);
      setTimeout(() => setSaved(false), 2000);
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
          <Settings size={20} className="text-slate-400" />
          系统设置
        </h2>
        <button
          onClick={handleSave}
          disabled={saving}
          className={`flex items-center gap-1.5 px-4 py-2 text-xs font-medium rounded-lg
                      transition-colors disabled:opacity-50 ${
                        saved
                          ? 'bg-green-600/30 text-green-300'
                          : 'bg-blue-600 hover:bg-blue-700 text-white'
                      }`}
        >
          {saving ? (
            <Loader2 size={14} className="animate-spin" />
          ) : saved ? (
            '✓ 已保存'
          ) : (
            <Save size={14} />
          )}
          {!saved && '保存设置'}
        </button>
      </div>

      {/* Web 服务器 + 录制路径 */}
      <Section icon={Server} title="服务器">
        <Field label="Web 服务器端口" hint="修改后需重启插件生效">
          <input
            type="number" min={80} max={65535}
            value={settings.webPort}
            onChange={e => update({ webPort: Number(e.target.value) })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors font-mono"
          />
        </Field>

        <Field label="录制默认路径">
          <input
            type="text"
            value={settings.recordingPath}
            onChange={e => update({ recordingPath: e.target.value })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors font-mono"
            placeholder="C:\Recordings"
          />
        </Field>
      </Section>

      {/* 编码器 + 时间码 */}
      <Section icon={Film} title="录制默认值">
        <Field label="默认编码器">
          <select
            value={settings.defaultEncoder}
            onChange={e => update({ defaultEncoder: e.target.value })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {ENCODER_OPTIONS.map(enc => (
              <option key={enc} value={enc}>{ENCODER_LABELS[enc] ?? enc}</option>
            ))}
          </select>
        </Field>

        <Field label="时间码帧率">
          <select
            value={settings.timecodeFps}
            onChange={e => update({ timecodeFps: Number(e.target.value) })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {FPS_OPTIONS.map(fps => (
              <option key={fps} value={fps}>{fps} fps</option>
            ))}
          </select>
        </Field>
      </Section>

      {/* 语言 */}
      <Section icon={Globe} title="界面">
        <Field label="语言（预留）">
          <select
            value={settings.language}
            onChange={e => update({ language: e.target.value })}
            className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                       text-slate-200 text-xs focus:outline-none focus:border-blue-500
                       transition-colors appearance-none cursor-pointer"
          >
            {LANGUAGE_OPTIONS.map(lang => (
              <option key={lang.value} value={lang.value}>{lang.label}</option>
            ))}
          </select>
        </Field>
      </Section>

      {/* 关于 */}
      <AboutSection />
    </div>
  );
}
