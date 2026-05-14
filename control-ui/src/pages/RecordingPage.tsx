// ============================================================
// 录制控制页 — obs-multicam-review
// 录制开始/暂停/停止 + 状态监控 + 录制配置
// ============================================================
import { useState, useEffect, useCallback } from 'react';
import {
  Radio, Pause, Square, Circle, Monitor, HardDrive, Clock,
  AlertTriangle, Loader2, Film, FolderOpen,
} from 'lucide-react';

// ── 类型 ──────────────────────────────────────────────

interface RecStatus {
  state: 'idle' | 'recording' | 'paused';
  duration: number;
  fileSize: number;
  droppedFrames: number;
  diskFreeBytes: number;
  outputDir: string;
}

interface RecConfig {
  mode: 'pgm' | 'isolated' | 'all';
  format: 'mkv' | 'mp4';
  encoder: 'nvenc' | 'qsv' | 'x264';
  outputDir: string;
}

// ── 常量 ──────────────────────────────────────────────

const REC_MODE_LABELS: Record<string, string> = {
  pgm: 'PGM 合成',
  isolated: '独立源',
  all: '全部',
};

const ENCODER_LABELS: Record<string, string> = {
  nvenc: 'NVENC',
  qsv: 'QSV (Intel)',
  x264: 'x264 (CPU)',
};

// ── 工具 ──────────────────────────────────────────────

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (h > 0) return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function formatBytes(b: number): string {
  if (b >= 1e9) return (b / 1e9).toFixed(2) + ' GB';
  if (b >= 1e6) return (b / 1e6).toFixed(1) + ' MB';
  if (b >= 1e3) return (b / 1e3).toFixed(0) + ' KB';
  return b + ' B';
}

// ── 脉冲动画 ──────────────────────────────────────────

const PULSE_STYLE = `
@keyframes recPulse {
  0%, 100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.7); }
  50%      { box-shadow: 0 0 0 14px rgba(239, 68, 68, 0); }
}
@keyframes recDot {
  0%, 100% { opacity: 1; }
  50%      { opacity: 0.3; }
}
.rec-pulse { animation: recPulse 2s ease-in-out infinite; }
.rec-dot   { animation: recDot 1s ease-in-out infinite; }
`;

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

// ── 状态指示卡片 ──────────────────────────────────────

function StatCard({ icon: Icon, label, value, color = 'text-slate-300' }: {
  icon: React.ComponentType<{ size?: number | string; className?: string }>;
  label: string;
  value: string;
  color?: string;
}) {
  return (
    <div className="bg-slate-800/50 backdrop-blur-sm border border-slate-700/60 rounded-xl px-4 py-3 flex items-center gap-3">
      <Icon size={18} className="text-slate-500 shrink-0" />
      <div>
        <div className="text-[10px] text-slate-500 uppercase tracking-wide">{label}</div>
        <div className={`text-sm font-mono font-semibold ${color}`}>{value}</div>
      </div>
    </div>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function RecordingPage() {
  const [status, setStatus] = useState<RecStatus>({
    state: 'idle',
    duration: 0,
    fileSize: 0,
    droppedFrames: 0,
    diskFreeBytes: 500 * 1e9,
    outputDir: 'C:\\Recordings',
  });

  const [config, setConfig] = useState<RecConfig>({
    mode: 'pgm',
    format: 'mkv',
    encoder: 'nvenc',
    outputDir: 'C:\\Recordings',
  });

  const [operating, setOperating] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const isRecording = status.state === 'recording';
  const isPaused = status.state === 'paused';

  // 轮询录制状态
  useEffect(() => {
    const poll = setInterval(async () => {
      try {
        // 模拟状态更新（实际应调用 /api/rec/status）
        if (status.state !== 'idle') {
          setStatus(prev => ({
            ...prev,
            duration: prev.duration + 0.5,
            fileSize: prev.fileSize + Math.random() * 500000,
            droppedFrames: prev.droppedFrames + (Math.random() > 0.95 ? 1 : 0),
          }));
        }
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    }, 500);
    return () => clearInterval(poll);
  }, [status.state]);

  // ── 操作 ──
  const handleStart = useCallback(async () => {
    setOperating(true);
    try {
      // await api.recStart();
      // 模拟
      setStatus(prev => ({ ...prev, state: 'recording', duration: 0, fileSize: 0, droppedFrames: 0 }));
    } catch (err) {
      alert('启动录制失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setOperating(false);
    }
  }, []);

  const handlePause = useCallback(async () => {
    setOperating(true);
    try {
      // await api.recPause();
      setStatus(prev => ({ ...prev, state: 'paused' }));
    } catch (err) {
      alert('暂停录制失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setOperating(false);
    }
  }, []);

  const handleResume = useCallback(async () => {
    setOperating(true);
    try {
      // await api.recResume();
      setStatus(prev => ({ ...prev, state: 'recording' }));
    } catch (err) {
      alert('恢复录制失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setOperating(false);
    }
  }, []);

  const handleStop = useCallback(async () => {
    setOperating(true);
    try {
      // await api.recStop();
      setStatus(prev => ({ ...prev, state: 'idle' }));
    } catch (err) {
      alert('停止录制失败: ' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setOperating(false);
    }
  }, []);

  // ── 渲染 ──
  if (error && status.state === 'idle') return <ErrorState error={error} />;

  return (
    <div className="max-w-3xl mx-auto space-y-6">
      <style>{PULSE_STYLE}</style>

      {/* 页头 */}
      <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
        <Radio size={20} className={isRecording ? 'text-red-400' : 'text-slate-400'} />
        录制控制
      </h2>

      {/* ── 控制按钮 ── */}
      <div className="flex items-center justify-center gap-6 py-4">
        {/* 开始 */}
        {status.state === 'idle' && (
          <button
            onClick={handleStart}
            disabled={operating}
            className="relative flex items-center justify-center w-20 h-20 bg-red-600
                       hover:bg-red-500 rounded-full shadow-lg shadow-red-600/30
                       disabled:opacity-50 transition-all active:scale-95"
            title="开始录制"
          >
            <Circle size={36} className="text-white fill-white" />
          </button>
        )}

        {/* 录制中：暂停 & 停止 */}
        {(isRecording || isPaused) && (
          <>
            {/* 停止 */}
            <button
              onClick={handleStop}
              disabled={operating}
              className="relative flex items-center justify-center w-16 h-16 bg-slate-700
                         hover:bg-slate-600 border-2 border-slate-500 rounded-full
                         disabled:opacity-50 transition-all active:scale-95"
              title="停止录制"
            >
              <Square size={24} className="text-white fill-white" />
            </button>

            {/* 暂停/恢复 */}
            <button
              onClick={isPaused ? handleResume : handlePause}
              disabled={operating}
              className={`relative flex items-center justify-center w-20 h-20 rounded-full
                          shadow-lg transition-all active:scale-95 disabled:opacity-50 ${
                            isRecording
                              ? 'rec-pulse bg-red-600 hover:bg-red-500 shadow-red-600/30'
                              : 'bg-yellow-500 hover:bg-yellow-400 shadow-yellow-500/30'
                          }`}
              title={isPaused ? '继续录制' : '暂停录制'}
            >
              {isPaused ? (
                <Circle size={38} className="text-white fill-white" />
              ) : (
                <Pause size={36} className="text-white fill-white" />
              )}
            </button>

            {/* 录制中指示 */}
            {isRecording && (
              <div className="flex items-center gap-2 text-red-400 ml-2">
                <span className="rec-dot w-3 h-3 bg-red-500 rounded-full" />
                <span className="text-xs font-bold uppercase tracking-widest">REC</span>
              </div>
            )}
            {isPaused && (
              <div className="flex items-center gap-2 text-yellow-400 ml-2">
                <span className="w-3 h-3 bg-yellow-500 rounded-full" />
                <span className="text-xs font-bold uppercase tracking-widest">PAUSED</span>
              </div>
            )}
          </>
        )}
      </div>

      {/* ── 状态指标 ── */}
      <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
        <StatCard
          icon={Clock}
          label="时长"
          value={formatDuration(status.duration)}
          color={isRecording ? 'text-red-300' : 'text-slate-300'}
        />
        <StatCard
          icon={HardDrive}
          label="文件大小"
          value={formatBytes(status.fileSize)}
        />
        <StatCard
          icon={Monitor}
          label="丢帧"
          value={String(status.droppedFrames)}
          color={status.droppedFrames > 10 ? 'text-red-400' : 'text-slate-300'}
        />
        <StatCard
          icon={HardDrive}
          label="磁盘剩余"
          value={formatBytes(status.diskFreeBytes)}
          color={status.diskFreeBytes < 10e9 ? 'text-red-400' : 'text-green-400'}
        />
      </div>

      {/* ── 录制配置（仅 idle 时可编辑） ── */}
      <div className="bg-slate-800/50 backdrop-blur-sm border border-slate-700/60 rounded-xl px-5 py-4 space-y-4">
        <h3 className="text-sm font-semibold text-slate-300 flex items-center gap-2">
          <Film size={16} className="text-slate-500" />
          录制设置
        </h3>

        {/* 录制模式 */}
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
          <ConfigGroup label="录制模式">
            {(['pgm', 'isolated', 'all'] as const).map(mode => (
              <label
                key={mode}
                className={`flex items-center gap-2 px-3 py-2 text-xs rounded-lg border cursor-pointer
                            transition-all ${
                              config.mode === mode
                                ? 'bg-blue-600/20 border-blue-500/50 text-blue-200'
                                : 'border-slate-700 text-slate-400 hover:border-slate-600'
                            } ${!isRecording && !isPaused ? '' : 'opacity-50 pointer-events-none'}`}
              >
                <input
                  type="radio" name="recMode" value={mode}
                  checked={config.mode === mode}
                  onChange={e => setConfig(prev => ({ ...prev, mode: e.target.value as RecConfig['mode'] }))}
                  className="sr-only"
                />
                {REC_MODE_LABELS[mode] ?? mode}
              </label>
            ))}
          </ConfigGroup>
        </div>

        {/* 格式 + 编码器 */}
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
          <ConfigField label="录制格式">
            <select
              value={config.format}
              onChange={e => setConfig(prev => ({ ...prev, format: e.target.value as RecConfig['format'] }))}
              disabled={isRecording || isPaused}
              className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2 text-slate-200
                         text-xs focus:outline-none focus:border-blue-500 transition-colors
                         disabled:opacity-50 appearance-none cursor-pointer"
            >
              <option value="mkv">MKV（推荐）</option>
              <option value="mp4">MP4</option>
            </select>
          </ConfigField>

          <ConfigField label="编码器">
            <select
              value={config.encoder}
              onChange={e => setConfig(prev => ({ ...prev, encoder: e.target.value as RecConfig['encoder'] }))}
              disabled={isRecording || isPaused}
              className="w-full bg-slate-900 border border-slate-600 rounded-lg px-3 py-2 text-slate-200
                         text-xs focus:outline-none focus:border-blue-500 transition-colors
                         disabled:opacity-50 appearance-none cursor-pointer"
            >
              {Object.entries(ENCODER_LABELS).map(([value, label]) => (
                <option key={value} value={value}>{label}</option>
              ))}
            </select>
          </ConfigField>
        </div>

        {/* 输出目录 */}
        <ConfigField label="输出目录">
          <div className="flex items-center gap-2">
            <input
              type="text"
              value={config.outputDir}
              onChange={e => setConfig(prev => ({ ...prev, outputDir: e.target.value }))}
              disabled={isRecording || isPaused}
              className="flex-1 bg-slate-900 border border-slate-600 rounded-lg px-3 py-2
                         text-slate-200 text-xs focus:outline-none focus:border-blue-500
                         transition-colors disabled:opacity-50 font-mono"
              placeholder="C:\Recordings"
            />
            <button
              disabled={isRecording || isPaused}
              className="p-2 bg-slate-700 hover:bg-slate-600 text-slate-300 rounded-lg
                         transition-colors disabled:opacity-50"
              title="浏览"
            >
              <FolderOpen size={16} />
            </button>
          </div>
        </ConfigField>
      </div>
    </div>
  );
}

// ── 辅助组件 ──────────────────────────────────────────

function ConfigGroup({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="sm:col-span-3">
      <div className="text-[10px] text-slate-500 uppercase tracking-wide mb-2">{label}</div>
      <div className="flex gap-2">{children}</div>
    </div>
  );
}

function ConfigField({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <div className="text-[10px] text-slate-500 uppercase tracking-wide mb-1.5">{label}</div>
      {children}
    </div>
  );
}
