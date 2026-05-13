// ============================================================
// 音频控制台 — obs-multicam-review
// 通道音量/静音/Solo/VU 表 + 高级 DSP 面板
// ============================================================
import { useState, useEffect, useCallback, useRef } from 'react';
import { Mic, Volume2, VolumeX, Headphones, ChevronDown, ChevronUp, Loader2, AlertTriangle } from 'lucide-react';

// ── 音频通道类型 ──────────────────────────────────────

interface AudioChannel {
  id: string;
  obsName: string;
  alias: string;
  volume: number;      // 0–1
  muted: boolean;
  solo: boolean;
  vuLevel: number;     // 0–1（模拟）
  peakLevel: number;   // 峰值
  peakHoldUntil: number;
  // DSP
  gain: number;        // -30…+30 dB
  noiseGate: number;   // -100…0 dB 阈值
  equalizer: { low: number; mid: number; high: number };
  compressor: { threshold: number; ratio: number };
  limiter: number;     // -20…0 dB 阈值
}

const DEFAULT_CHANNEL: Omit<AudioChannel, 'id' | 'obsName' | 'alias'> = {
  volume: 0.8,
  muted: false,
  solo: false,
  vuLevel: 0,
  peakLevel: 0,
  peakHoldUntil: 0,
  gain: 0,
  noiseGate: -60,
  equalizer: { low: 0, mid: 0, high: 0 },
  compressor: { threshold: -20, ratio: 2 },
  limiter: -3,
};

// ── 工具函数 ──────────────────────────────────────────

function volToDb(v: number) {
  if (v <= 0) return '-∞';
  const db = 20 * Math.log10(v);
  return (db >= 0 ? '+' : '') + db.toFixed(1) + ' dB';
}

function dbToVol(db: number) {
  return Math.pow(10, db / 20);
}

// ── 符号转百分比（0–1）───────────────────────────────
function vToPct(v: number) { return Math.round(v * 100); }

// ── VU 颜色计算 ───────────────────────────────────────
function vuColor(level: number) {
  if (level >= 0.95) return 'bg-red-500';
  if (level >= 0.75) return 'bg-yellow-500';
  if (level >= 0.5) return 'bg-yellow-400';
  return 'bg-green-400';
}

// ── 模拟通道数据 ──────────────────────────────────────
const MOCK_CHANNELS: AudioChannel[] = [
  { ...DEFAULT_CHANNEL, id: 'ch-1', obsName: 'Mic/Aux', alias: '主持人', volume: 0.85, vuLevel: 0.45, peakLevel: 0.55 },
  { ...DEFAULT_CHANNEL, id: 'ch-2', obsName: 'Desktop Audio', alias: '系统音效', volume: 0.6, vuLevel: 0.15, peakLevel: 0.22 },
  { ...DEFAULT_CHANNEL, id: 'ch-3', obsName: 'Mic 2', alias: '嘉宾麦克风', volume: 0.75, vuLevel: 0.32, peakLevel: 0.40 },
  { ...DEFAULT_CHANNEL, id: 'ch-4', obsName: 'Media Source', alias: '背景音乐', volume: 0.4, vuLevel: 0.08, peakLevel: 0.12 },
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

// ── 单通道行 ──────────────────────────────────────────

function ChannelRow({
  ch,
  isSoloed,
  onVolume,
  onMute,
  onSolo,
  onChange,
}: {
  ch: AudioChannel;
  isSoloed: boolean;
  onVolume: (id: string, v: number) => void;
  onMute: (id: string) => void;
  onSolo: (id: string) => void;
  onChange: (id: string, patch: Partial<AudioChannel>) => void;
}) {
  const [expanded, setExpanded] = useState(false);
  const peakRef = useRef<number>(0);
  const holdTimer = useRef<ReturnType<typeof setTimeout>>();

  const pct = vToPct(ch.volume);
  const displayLevel = Math.max(ch.vuLevel, ch.peakLevel > ch.vuLevel ? ch.peakLevel : 0);

  return (
    <div className="bg-slate-800/50 backdrop-blur-sm border border-slate-700/60 rounded-xl overflow-hidden">
      {/* 主行 */}
      <div className="flex items-center gap-3 px-4 py-3">
        {/* 标签 */}
        <div className="w-28 shrink-0 min-w-0">
          <div className="text-sm font-medium text-slate-200 truncate">{ch.alias}</div>
          <div className="text-[11px] text-slate-500 truncate">{ch.obsName}</div>
        </div>

        {/* VU 条 */}
        <div className="flex-1 h-7 bg-slate-900 border border-slate-700 rounded relative overflow-hidden mr-1">
          {/* 刻度线 */}
          <div className="absolute left-1/4 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
          <div className="absolute left-1/2 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
          <div className="absolute left-3/4 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
          <div className="absolute left-[95%] top-0 bottom-0 w-px bg-red-500/30 z-10" />

          {/* VU 电平 */}
          <div
            className="h-full transition-all duration-75 ease-out rounded-r"
            style={{ width: `${Math.min(displayLevel * 100, 100)}%` }}
          >
            <div
              className="h-full rounded-r"
              style={{
                background: displayLevel >= 0.95
                  ? 'linear-gradient(90deg, #22c55e 0%, #eab308 60%, #ef4444 85%, #ef4444 100%)'
                  : displayLevel >= 0.75
                    ? 'linear-gradient(90deg, #22c55e 0%, #eab308 70%, #ef4444 95%)'
                    : 'linear-gradient(90deg, #22c55e 0%, #eab308 85%)',
              }}
            />
          </div>

          {/* 峰值线 */}
          {ch.peakLevel > 0.01 && (
            <div
              className="absolute top-0 bottom-0 w-0.5 bg-white/70 z-20"
              style={{ left: `${Math.min(ch.peakLevel * 100, 100)}%` }}
            />
          )}

          {/* dB 值 */}
          <span className="absolute right-1.5 top-1/2 -translate-y-1/2 text-[10px] font-mono text-slate-300 z-20 bg-slate-900/80 px-1 rounded">
            {volToDb(displayLevel || ch.vuLevel)}
          </span>
        </div>

        {/* 音量推子 */}
        <div className="flex items-center gap-1">
          <Volume2 size={14} className="text-slate-400" />
          <input
            type="range"
            min={0}
            max={100}
            value={pct}
            onChange={e => onVolume(ch.id, Number(e.target.value) / 100)}
            className="w-20 h-1.5 bg-slate-700 rounded-lg appearance-none cursor-pointer
                       [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-3
                       [&::-webkit-slider-thumb]:h-3 [&::-webkit-slider-thumb]:bg-green-400
                       [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:cursor-pointer
                       [&::-webkit-slider-thumb]:shadow-md"
          />
          <span className="text-[11px] font-mono text-slate-400 w-11 text-right">
            {volToDb(ch.volume)}
          </span>
        </div>

        {/* 静音 */}
        <button
          onClick={() => onMute(ch.id)}
          className={`p-2 rounded-lg transition-all ${
            ch.muted
              ? 'bg-red-500/20 text-red-400 hover:bg-red-500/30'
              : 'text-slate-500 hover:text-slate-300 hover:bg-slate-700'
          }`}
          title="静音"
        >
          {ch.muted ? <VolumeX size={16} /> : <Volume2 size={16} />}
        </button>

        {/* Solo */}
        <button
          onClick={() => onSolo(ch.id)}
          className={`p-2 rounded-lg transition-all ${
            ch.solo
              ? 'bg-yellow-500/20 text-yellow-400 ring-1 ring-yellow-500/50'
              : 'text-slate-500 hover:text-slate-300 hover:bg-slate-700'
          }`}
          title="Solo"
        >
          <Headphones size={16} />
        </button>

        {/* 展开 */}
        <button
          onClick={() => setExpanded(v => !v)}
          className="p-1.5 text-slate-600 hover:text-slate-300 transition-colors"
        >
          {expanded ? <ChevronUp size={14} /> : <ChevronDown size={14} />}
        </button>
      </div>

      {/* 高级面板 */}
      {expanded && (
        <div className="border-t border-slate-700/40 px-4 py-3 bg-slate-800/30 space-y-3">
          <DspSection label="增益" unit="dB" min={-30} max={30} value={ch.gain}
            onChange={v => onChange(ch.id, { gain: v })} />
          <DspSection label="噪声门" unit="dB" min={-100} max={0} value={ch.noiseGate}
            onChange={v => onChange(ch.id, { noiseGate: v })} />
          <div className="grid grid-cols-3 gap-4">
            <DspSection label="EQ 低" unit="dB" min={-12} max={12} value={ch.equalizer.low}
              onChange={v => onChange(ch.id, { equalizer: { ...ch.equalizer, low: v } })} />
            <DspSection label="EQ 中" unit="dB" min={-12} max={12} value={ch.equalizer.mid}
              onChange={v => onChange(ch.id, { equalizer: { ...ch.equalizer, mid: v } })} />
            <DspSection label="EQ 高" unit="dB" min={-12} max={12} value={ch.equalizer.high}
              onChange={v => onChange(ch.id, { equalizer: { ...ch.equalizer, high: v } })} />
          </div>
          <div className="grid grid-cols-2 gap-4">
            <DspSection label="压缩 阈值" unit="dB" min={-60} max={0} value={ch.compressor.threshold}
              onChange={v => onChange(ch.id, { compressor: { ...ch.compressor, threshold: v } })} />
            <DspSection label="压缩 比例" unit=":1" min={1} max={20} step={0.5} value={ch.compressor.ratio}
              onChange={v => onChange(ch.id, { compressor: { ...ch.compressor, ratio: v } })} />
          </div>
          <DspSection label="限制器" unit="dB" min={-20} max={0} value={ch.limiter}
            onChange={v => onChange(ch.id, { limiter: v })} />
        </div>
      )}
    </div>
  );
}

// ── DSP Slider ────────────────────────────────────────

function DspSection({
  label, unit, min, max, step = 1, value, onChange,
}: {
  label: string; unit: string; min: number; max: number; step?: number; value: number; onChange: (v: number) => void;
}) {
  return (
    <div className="flex items-center gap-2">
      <span className="text-[11px] text-slate-500 w-16 shrink-0">{label}</span>
      <input
        type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(Number(e.target.value))}
        className="flex-1 h-1 bg-slate-700 rounded-lg appearance-none cursor-pointer
                   [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-2.5
                   [&::-webkit-slider-thumb]:h-2.5 [&::-webkit-slider-thumb]:bg-blue-400
                   [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:cursor-pointer"
      />
      <span className="text-[11px] font-mono text-slate-400 w-12 text-right">
        {step < 1 ? value.toFixed(1) : value}{unit}
      </span>
    </div>
  );
}

// ── 主音量 ────────────────────────────────────────────

function MasterSection({
  masterVol,
  masterMuted,
  onMasterVol,
  onMasterMute,
}: {
  masterVol: number;
  masterMuted: boolean;
  onMasterVol: (v: number) => void;
  onMasterMute: () => void;
}) {
  const pct = vToPct(masterVol);

  return (
    <div className="bg-slate-800/70 backdrop-blur-sm border border-slate-700/60 rounded-xl px-4 py-3 flex items-center gap-4">
      <span className="text-sm font-semibold text-slate-300 shrink-0">主输出</span>

      {/* 主 VU */}
      <div className="flex-1 h-8 bg-slate-900 border border-slate-700 rounded relative overflow-hidden">
        <div className="absolute left-1/4 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
        <div className="absolute left-1/2 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
        <div className="absolute left-3/4 top-0 bottom-0 w-px bg-slate-600/40 z-10" />
        <div className="absolute right-4 top-1/2 -translate-y-1/2 text-[10px] font-mono text-slate-500 z-20">
          {volToDb(masterMuted ? 0 : masterVol)}
        </div>
        <div
          className="h-full transition-all duration-75 ease-out rounded-r"
          style={{
            width: `${masterMuted ? 0 : masterVol * 100}%`,
            background: masterVol >= 0.95
              ? 'linear-gradient(90deg, #22c55e 0%, #eab308 60%, #ef4444 85%, #ef4444 100%)'
              : 'linear-gradient(90deg, #22c55e 0%, #eab308 85%)',
          }}
        />
      </div>

      {/* 主推子 */}
      <Volume2 size={14} className="text-slate-400" />
      <input
        type="range" min={0} max={100} value={pct}
        onChange={e => onMasterVol(Number(e.target.value) / 100)}
        className="w-24 h-1.5 bg-slate-700 rounded-lg appearance-none cursor-pointer
                   [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-3.5
                   [&::-webkit-slider-thumb]:h-3.5 [&::-webkit-slider-thumb]:bg-cyan-400
                   [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:cursor-pointer"
      />
      <span className="text-[11px] font-mono text-slate-400 w-11 text-right">
        {masterMuted ? 'MUTED' : volToDb(masterVol)}
      </span>
      <button
        onClick={onMasterMute}
        className={`px-3 py-1.5 text-xs font-medium rounded-lg transition-all ${
          masterMuted
            ? 'bg-red-500/20 text-red-400 ring-1 ring-red-500/40'
            : 'bg-slate-700 text-slate-300 hover:bg-slate-600'
        }`}
      >
        {masterMuted ? '静音中' : 'Mute All'}
      </button>
    </div>
  );
}

// ── 页面主体 ──────────────────────────────────────────

export default function AudioConsolePage() {
  const [channels, setChannels] = useState<AudioChannel[]>(MOCK_CHANNELS);
  const [masterVol, setMasterVol] = useState(0.8);
  const [masterMuted, setMasterMuted] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // 模拟 VU 波动
  useEffect(() => {
    const interval = setInterval(() => {
      setChannels(prev =>
        prev.map(ch => {
          const newLevel = Math.max(0, Math.min(1,
            ch.volume * (0.3 + Math.random() * 0.7)
          ));
          const peak = Math.max(ch.peakLevel, newLevel);
          return {
            ...ch,
            vuLevel: newLevel,
            peakLevel: peak,
          };
        }),
      );
    }, 200);
    return () => clearInterval(interval);
  }, []);

  // 峰值保持衰减
  useEffect(() => {
    const dec = setInterval(() => {
      setChannels(prev =>
        prev.map(ch => ({
          ...ch,
          peakLevel: Math.max(0, ch.peakLevel - 0.015),
        })),
      );
    }, 100);
    return () => clearInterval(dec);
  }, []);

  // ── 操作 ──
  const handleVolume = useCallback((id: string, v: number) => {
    setChannels(prev => prev.map(ch => (ch.id === id ? { ...ch, volume: v } : ch)));
  }, []);

  const handleMute = useCallback((id: string) => {
    setChannels(prev => prev.map(ch => (ch.id === id ? { ...ch, muted: !ch.muted } : ch)));
  }, []);

  const handleSolo = useCallback((id: string) => {
    setChannels(prev =>
      prev.map(ch => ({ ...ch, solo: ch.id === id ? !ch.solo : false })),
    );
  }, []);

  const handleChange = useCallback((id: string, patch: Partial<AudioChannel>) => {
    setChannels(prev => prev.map(ch => (ch.id === id ? { ...ch, ...patch } : ch)));
  }, []);

  if (error) return <ErrorState error={error} />;

  return (
    <div className="max-w-5xl mx-auto space-y-5">
      {/* 页头 */}
      <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
        <Mic size={20} className="text-green-400" />
        音频控制台
      </h2>

      {/* 主输出 */}
      <MasterSection
        masterVol={masterVol}
        masterMuted={masterMuted}
        onMasterVol={setMasterVol}
        onMasterMute={() => setMasterMuted(v => !v)}
      />

      {/* 通道列表 */}
      <div className="space-y-2">
        {channels.map(ch => (
          <ChannelRow
            key={ch.id}
            ch={ch}
            isSoloed={ch.solo}
            onVolume={handleVolume}
            onMute={handleMute}
            onSolo={handleSolo}
            onChange={handleChange}
          />
        ))}
      </div>
    </div>
  );
}
