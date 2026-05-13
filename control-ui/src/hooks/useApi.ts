// ============================================================
// 通用 API 调用 Hook
// ============================================================
import { useState, useEffect, useCallback } from 'react';

export function useApi<T>(
  fetcher: () => Promise<T>,
  deps: unknown[] = [],
) {
  const [data, setData] = useState<T | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(() => {
    setLoading(true);
    setError(null);
    fetcher()
      .then(setData)
      .catch((e) => setError(e instanceof Error ? e.message : String(e)))
      .finally(() => setLoading(false));
  }, deps); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => { refresh(); }, [refresh]);

  return { data, loading, error, refresh };
}
