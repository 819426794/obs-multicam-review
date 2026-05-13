// ============================================================
// WebSocket 管理器 — obs-multicam-review
// 自动重连 + 心跳维持 + 事件订阅
// ============================================================

import type { WsEventAction, WsCommandAction } from '../types/api';

type Handler = (payload: unknown) => void;

class WebSocketManager {
  private ws: WebSocket | null = null;
  private url: string;
  private handlers = new Map<string, Set<Handler>>();
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private lastPong = 0;
  private retryDelay = 1000;
  private maxDelay = 30000;
  private _connected = false;
  private listeners: Array<(connected: boolean) => void> = [];

  constructor(url: string) {
    this.url = url;
  }

  connect() {
    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
      return;
    }

    try {
      this.ws = new WebSocket(this.url);
    } catch (err) {
      console.error('[ws] Failed to create WebSocket:', err);
      this.scheduleReconnect();
      return;
    }

    this.ws.onopen = () => {
      console.info('[ws] Connected');
      this._connected = true;
      this.retryDelay = 1000;
      this.lastPong = Date.now();
      this.startHeartbeat();
      this.notifyListeners(true);
    };

    this.ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        const action = msg.action as string;
        const subs = this.handlers.get(action);
        if (subs) {
          subs.forEach((fn) => {
            try { fn(msg.payload ?? msg); } catch (e) { console.error('[ws] Handler error:', e); }
          });
        }
      } catch (e) {
        console.error('[ws] Parse error:', e);
      }
    };

    this.ws.onclose = () => {
      console.info('[ws] Disconnected');
      this._connected = false;
      this.stopHeartbeat();
      this.notifyListeners(false);
      this.scheduleReconnect();
    };

    this.ws.onerror = () => {
      // onclose will follow, handle there
    };
  }

  disconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.stopHeartbeat();
    this._connected = false;
    if (this.ws) {
      this.ws.onclose = null; // prevent reconnect
      this.ws.close();
      this.ws = null;
    }
    this.notifyListeners(false);
  }

  send(action: WsCommandAction | string, payload?: unknown) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[ws] Not connected, cannot send:', action);
      return;
    }
    this.ws.send(JSON.stringify({ type: 'request', id: crypto.randomUUID(), action, payload }));
  }

  onEvent(action: WsEventAction | string, handler: Handler): () => void {
    if (!this.handlers.has(action)) this.handlers.set(action, new Set());
    this.handlers.get(action)!.add(handler);
    return () => {
      this.handlers.get(action)?.delete(handler);
    };
  }

  isConnected(): boolean {
    return this._connected;
  }

  onConnectChange(fn: (connected: boolean) => void): () => void {
    this.listeners.push(fn);
    return () => {
      this.listeners = this.listeners.filter((l) => l !== fn);
    };
  }

  // ============ 内部 ============

  private notifyListeners(connected: boolean) {
    this.listeners.forEach((fn) => {
      try { fn(connected); } catch { /* ignore */ }
    });
  }

  private startHeartbeat() {
    this.heartbeatTimer = setInterval(() => {
      if (Date.now() - this.lastPong > 30000) {
        console.warn('[ws] Heartbeat timeout, reconnecting...');
        this.ws?.close();
        return;
      }
    }, 5000);

    // 监听心跳事件
    const unsub = this.onEvent('system.heartbeat', () => {
      this.lastPong = Date.now();
      this.send('system.pong');
    });
    // unsub on disconnect
    this.onEvent('system.status', () => {});
  }

  private stopHeartbeat() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  private scheduleReconnect() {
    if (this.reconnectTimer) return;
    console.info(`[ws] Reconnecting in ${this.retryDelay}ms...`);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
      this.retryDelay = Math.min(this.retryDelay * 2, this.maxDelay);
    }, this.retryDelay);
  }
}

export const wsManager = new WebSocketManager(
  `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`,
);
