import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom';
import { useState } from 'react';
import {
  LayoutDashboard, Camera, Mic, Star, Clapperboard,
  Monitor, Settings, Play, Square, List, Clock,
} from 'lucide-react';

const navItems = [
  { to: '/', icon: LayoutDashboard, label: '概览' },
  { to: '/sources', icon: Camera, label: '输入源' },
  { to: '/audio', icon: Mic, label: '音频控制台' },
  { to: '/scoring', icon: Star, label: '评分' },
  { to: '/overlay', icon: Clapperboard, label: '叠加层' },
  { to: '/presets', icon: List, label: '预设' },
  { to: '/settings', icon: Settings, label: '设置' },
];

function App() {
  const [recording, setRecording] = useState(false);
  const [connected, setConnected] = useState(false);

  return (
    <BrowserRouter>
      <div className="h-full flex flex-col">
        {/* 顶栏 */}
        <header className="bg-surface border-b border-slate-700 px-4 py-2 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <Monitor className="text-primary" size={20} />
            <h1 className="text-sm font-bold tracking-wide">多机位评测控制台</h1>
          </div>
          <div className="flex items-center gap-3">
            <span className={`inline-block w-2 h-2 rounded-full ${connected ? 'bg-green-400' : 'bg-red-400'}`} />
            <span className="text-xs text-slate-400">{connected ? '已连接' : '未连接'}</span>

            <button
              onClick={() => setRecording(!recording)}
              className={`flex items-center gap-1 px-3 py-1 rounded text-xs font-mono
                ${recording
                  ? 'bg-red-600 hover:bg-red-700 text-white'
                  : 'bg-primary hover:bg-primary-dark text-white'}`}
            >
              {recording ? <Square size={14} /> : <Play size={14} />}
              {recording ? '停止' : '开始录制'}
            </button>
          </div>
        </header>

        <div className="flex flex-1 overflow-hidden">
          {/* 侧边栏 */}
          <nav className="w-48 bg-surface border-r border-slate-700 flex flex-col py-2">
            {navItems.map(({ to, icon: Icon, label }) => (
              <NavLink
                key={to}
                to={to}
                className={({ isActive }) =>
                  `flex items-center gap-2 px-3 py-2 text-xs transition-colors
                  ${isActive ? 'bg-primary/20 text-primary border-r-2 border-primary' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'}`}
              >
                <Icon size={14} />
                {label}
              </NavLink>
            ))}
          </nav>

          {/* 主区域 */}
          <main className="flex-1 overflow-auto p-4">
            <Routes>
              <Route path="/" element={<Dashboard connected={connected} setConnected={setConnected} />} />
              <Route path="/sources" element={<Placeholder title="输入源管理" />} />
              <Route path="/audio" element={<Placeholder title="音频控制台" />} />
              <Route path="/scoring" element={<Placeholder title="评分系统" />} />
              <Route path="/overlay" element={<Placeholder title="叠加层编辑" />} />
              <Route path="/presets" element={<Placeholder title="预设管理" />} />
              <Route path="/settings" element={<Placeholder title="系统设置" />} />
            </Routes>
          </main>
        </div>

        {/* 底栏状态 */}
        <footer className="bg-surface border-t border-slate-700 px-4 py-1 flex items-center gap-6 text-xs text-slate-500">
          <span><Clock size={12} className="inline mr-1" />00:00:00.00</span>
          <span>帧率: 60</span>
          <span>磁盘: 245 GB 可用</span>
        </footer>
      </div>
    </BrowserRouter>
  );
}

function Dashboard({ connected, setConnected }: { connected: boolean; setConnected: (v: boolean) => void }) {
  return (
    <div className="space-y-4">
      <div className="bg-surface-light rounded-lg p-4 border border-slate-700">
        <h2 className="text-lg font-bold mb-2">系统概览</h2>
        <p className="text-slate-400 text-sm">插件状态: {connected ? '运行中' : '未检测到'}</p>
        <button
          onClick={() => setConnected(!connected)}
          className="mt-2 px-3 py-1 bg-primary/20 text-primary rounded text-xs hover:bg-primary/30"
        >
          {connected ? '模拟断开' : '模拟连接'}
        </button>
      </div>
    </div>
  );
}

function Placeholder({ title }: { title: string }) {
  return (
    <div className="flex items-center justify-center h-full">
      <div className="text-center text-slate-500">
        <h2 className="text-xl font-bold mb-2">{title}</h2>
        <p className="text-sm">模块开发中...</p>
      </div>
    </div>
  );
}

export default App;
