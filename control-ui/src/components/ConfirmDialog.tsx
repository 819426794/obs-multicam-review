import { Modal } from './Modal';

interface ConfirmDialogProps {
  open: boolean;
  onClose: () => void;
  onConfirm: () => void;
  title: string;
  message: string;
  confirmLabel?: string;
  danger?: boolean;
}

export function ConfirmDialog({
  open, onClose, onConfirm, title, message, confirmLabel, danger = true,
}: ConfirmDialogProps) {
  return (
    <Modal open={open} onClose={onClose} title={title} width="sm">
      <p className="text-slate-300 text-sm mb-6">{message}</p>
      <div className="flex justify-end gap-3">
        <button
          onClick={onClose}
          className="px-4 py-2 text-sm text-slate-400 hover:text-slate-200 hover:bg-slate-800 rounded-lg transition-colors"
        >
          取消
        </button>
        <button
          onClick={() => { onConfirm(); onClose(); }}
          className={`px-4 py-2 text-sm text-white rounded-lg transition-colors ${
            danger ? 'bg-red-600 hover:bg-red-700' : 'bg-primary hover:bg-primary-dark'
          }`}
        >
          {confirmLabel ?? (danger ? '删除' : '确认')}
        </button>
      </div>
    </Modal>
  );
}
