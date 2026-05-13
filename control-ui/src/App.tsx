import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom';
import {
  LayoutDashboard, Camera, Mic, Star, Clapperboard,
  Monitor, List, Clock, FolderKanban,
} from 'lucide-react';
import { useApi } from './hooks/useApi';
import * as api from './services/api-client';
import ProjectsPage from './pages/ProjectsPage';
import ProductsPage from './pages/ProductsPage';
import SourcesPage from './pages/SourcesPage';
import ScenesPage from './pages/ScenesPage';

const navItems = [
  { to: '/', icon: LayoutDashboard, label: '概览' },
  { to: '/sources', icon: Camera, label: '输入源' },
  { to: '/scenes', icon: Clapperboard, label: '场景' },
  { to: '/audio', icon: Mic, label: '音频控制台' },
  { to: '/projects', icon: FolderKanban, label: '项目管理' },
  { to: '/overlay', icon: Clapperboard, label: '叠加层' },
  { to: '/presets', icon: List, label: '预设' },
  { to: '/settings', icon: Star, label: '评分' },
];

function App() {
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
            <ConnectionStatus />
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
              <Route path="/" element={<Dashboard />} />
              <Route path="/sources" element={<SourcesPage />} />
              <Route path="/scenes" element={<ScenesPage />} />
              <Route path="/audio" element={<Placeholder title="音频控制台" />} />
              <Route path="/projects" element={<ProjectsPage />} />
              <Route path="/products/:projectId" element={<ProductsPage />} />
              <Route path="/overlay" element={<Placeholder title="叠加层编辑" />} />
              <Route path="/presets" element={<Placeholder title="预设管理" />} />
              <Route path="/settings" element={<Placeholder title="评分系统" />} />
            </Routes>
          </main>
        </div>

        {/* 底栏状态 */}
        <footer className="bg-surface border-t border-slate-700 px-4 py-1 flex items-center gap-6 text-xs text-slate-500">
          <span><Clock size={12} className="inline mr-1" />--:--:--.--</span>
          <span>帧率: --</span>
          <span>磁盘: -- GB 可用</span>
        </footer>
      </div>
    </BrowserRouter>
  );
}

// ============ 连接状态（顶栏） ============
function ConnectionStatus() {
  const { data: status, loading, error } = useApi(() => api.fetchSystemStatus(), []);

  const connected = !!status && !error;
  const isRecording = status?.recordingState === 'recording';

  return (
    <div className="flex items-center gap-3">
      {loading ? (
        <span className="text-xs text-slate-500">检测中...</span>
      ) : (
        <>
          <span
            className={`inline-block w-2 h-2 rounded-full ${
              connected ? 'bg-green-400' : 'bg-red-400'
            }`}
          />
          <span className="text-xs text-slate-400">
            {connected ? '已连接' : '未连接'}
          </span>
          {connected && (
            <span className="text-xs text-slate-500">
              {isRecording ? '录制中' : '待机'}
            </span>
          )}
        </>
      )}
    </div>
  );
}

// ============ 概览仪表盘 ============
function Dashboard() {
  const { data: status, loading, error } = useApi(() => api.fetchSystemStatus(), []);

  return (
    <div className="space-y-4">
      <div className="bg-surface-light rounded-lg p-4 border border-slate-700">
        <h2 className="text-lg font-bold mb-2">系统概览</h2>
        {loading ? (
          <p className="text-slate-500 text-sm">加载中...</p>
        ) : error ? (
          <p className="text-red-400 text-sm">连接失败: {error}</p>
        ) : status ? (
          <div className="space-y-1 text-sm text-slate-300">
            <p>
              插件状态:{' '}
              <span className="text-green-400">运行中 v{status.pluginVersion}</span>
            </p>
            {status.obsVersion && (
              <p>OBS 版本: <span className="text-slate-400">{status.obsVersion}</span></p>
            )}
            <p>
              录制状态:{' '}
              <span className={status.recordingState === 'recording' ? 'text-red-400' : 'text-slate-400'}>
                {status.recordingState === 'recording' ? '录制中' : status.recordingState === 'paused' ? '已暂停' : '待机'}
              </span>
            </p>
            {status.currentScene && (
              <p>当前场景: <span className="text-slate-400">{status.currentScene}</span></p>
            )}
            {status.timecode && (
              <p>时码: <span className="text-slate-400 font-mono">{status.timecode}</span></p>
            )}
            {status.fps > 0 && (
              <p>帧率: <span className="text-slate-400">{status.fps} fps</span></p>
            )}
            {status.cpuUsage !== undefined && (
              <p>CPU: <span className="text-slate-400">{status.cpuUsage}%</span></p>
            )}
            {status.memoryUsageMB !== undefined && (
              <p>内存: <span className="text-slate-400">{status.memoryUsageMB} MB</span></p>
            )}
            {status.diskFreeBytes > 0 && (
              <p>
                磁盘:{' '}
                <span className="text-slate-400">
                  {(status.diskFreeBytes / 1e9).toFixed(1)} GB 可用
                </span>
              </p>
            )}
          </div>
        ) : (
          <p className="text-slate-500 text-sm">无数据</p>
        )}
      </div>
    </div>
  );
}

// ============ 占位页面 ============
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
