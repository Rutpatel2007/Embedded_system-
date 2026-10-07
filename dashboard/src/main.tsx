import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { Layout } from './components/Layout';
import { OverviewPage } from './pages/OverviewPage';
import { LiveMonitorPage } from './pages/LiveMonitorPage';
import { IntegrityPage } from './pages/IntegrityPage';
import { AlertsPage } from './pages/AlertsPage';
import { MLAdvisoryPage } from './pages/MLAdvisoryPage';
import { ArchitecturePage } from './pages/ArchitecturePage';
import { ProjectStatusPage } from './pages/ProjectStatusPage';
import './index.css';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: 1,
      refetchOnWindowFocus: false,
    },
  },
});

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<OverviewPage />} />
            <Route path="live" element={<LiveMonitorPage />} />
            <Route path="integrity" element={<IntegrityPage />} />
            <Route path="alerts" element={<AlertsPage />} />
            <Route path="ml-advisory" element={<MLAdvisoryPage />} />
            <Route path="architecture" element={<ArchitecturePage />} />
            <Route path="status" element={<ProjectStatusPage />} />
            <Route path="*" element={<Navigate to="/" replace />} />
          </Route>
        </Routes>
      </BrowserRouter>
    </QueryClientProvider>
  </StrictMode>
);
