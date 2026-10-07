import { NavLink, Outlet, useLocation } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';
import { Badge } from './ui/Badge';
import { cn } from '../lib/utils';

const NAV_ITEMS = [
  { path: '/', label: 'Overview' },
  { path: '/live', label: 'Live Monitor' },
  { path: '/integrity', label: 'Integrity' },
  { path: '/alerts', label: 'Alerts' },
  { path: '/ml-advisory', label: 'ML Advisory' },
  { path: '/architecture', label: 'Architecture' },
  { path: '/status', label: 'Project Status' },
];

export function Layout() {
  const location = useLocation();

  const { data: health, isError: isHealthError } = useQuery({
    queryKey: ['health'],
    queryFn: () => api.getHealth(),
    refetchInterval: 10000,
  });

  return (
    <div className="min-h-screen flex flex-col bg-surface text-ink font-sans">
      {/* Honesty Banner: Always visible & prominent */}
      <aside 
        aria-label="Simulation Notice"
        className="w-full bg-ochre-subtle border-b border-ochre/30 px-4 py-1.5 text-center text-xs font-medium text-ochre flex items-center justify-center gap-2"
      >
        <span className="w-1.5 h-1.5 rounded-full bg-ochre shrink-0" />
        <span>Simulation data — not hardware validated. MQ-135 voltage proxies and uncalibrated firmware thresholds.</span>
      </aside>

      {/* Primary Top Bar */}
      <header className="border-b border-hairline bg-surface-elevated sticky top-0 z-20">
        <div className="max-w-[1200px] mx-auto px-4 sm:px-6 h-14 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="flex items-center gap-2">
              <span className="w-2.5 h-2.5 bg-teal" />
              <h1 className="text-sm font-bold tracking-tight text-ink uppercase font-mono-num">
                TRUESENSE <span className="font-normal text-muted">/ PROOV</span>
              </h1>
            </div>
            <span className="text-muted/40">|</span>
            <span className="text-xs text-muted font-mono-num hidden sm:inline">
              NODE: TS001
            </span>
          </div>

          <div className="flex items-center gap-3">
            {isHealthError ? (
              <Badge variant="critical" size="sm">
                BACKEND OFFLINE
              </Badge>
            ) : health?.status === 'healthy' ? (
              <Badge variant="verified" size="sm">
                BACKEND ONLINE
              </Badge>
            ) : (
              <Badge variant="neutral" size="sm">
                CONNECTING...
              </Badge>
            )}

            <button
              onClick={() => {
                document.documentElement.classList.toggle('dark');
              }}
              title="Toggle theme"
              className="text-xs border border-hairline px-2 py-1 bg-surface hover:bg-surface/80 font-mono text-muted hover:text-ink cursor-pointer"
            >
              THEME
            </button>
          </div>
        </div>

        {/* Navigation Tabs */}
        <nav aria-label="Main Navigation" className="border-t border-hairline bg-surface px-4 sm:px-6">
          <div className="max-w-[1200px] mx-auto flex items-center gap-1 overflow-x-auto scrollbar-none py-1">
            {NAV_ITEMS.map((item) => {
              const isActive = location.pathname === item.path;
              return (
                <NavLink
                  key={item.path}
                  to={item.path}
                  className={cn(
                    'px-3 py-1.5 text-xs font-medium whitespace-nowrap transition-colors duration-100 border-b-2',
                    isActive
                      ? 'border-teal text-teal font-semibold'
                      : 'border-transparent text-muted hover:text-ink hover:border-hairline'
                  )}
                >
                  {item.label}
                </NavLink>
              );
            })}
          </div>
        </nav>
      </header>

      {/* Main Content Area */}
      <main className="flex-1 max-w-[1200px] w-full mx-auto p-4 sm:p-6">
        <Outlet />
      </main>

      {/* Minimal Editorial Footer */}
      <footer className="border-t border-hairline py-4 px-4 sm:px-6 text-xs text-muted bg-surface">
        <div className="max-w-[1200px] mx-auto flex flex-col sm:flex-row items-center justify-between gap-2 font-mono-num">
          <div>
            TrueSense (PROOV) — Physical-Cryptographic Tamper-Evident Sensor System
          </div>
          <div className="flex items-center gap-4 text-[11px]">
            <span>ESP32-WROOM-32</span>
            <span>•</span>
            <span>SHA-256 Hash Chain</span>
            <span>•</span>
            <span>FastAPI Contract v1</span>
          </div>
        </div>
      </footer>
    </div>
  );
}
