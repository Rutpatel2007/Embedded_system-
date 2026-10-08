import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Skeleton } from '../components/ui/Primitives';

export function MLAdvisoryPage() {
  const { data, isLoading, isError } = useQuery({
    queryKey: ['mlStatus'],
    queryFn: () => api.getMLStatus(),
    retry: false,
  });

  return (
    <div className="space-y-6">
      <Card>
        <CardHeader
          title="ML ADVISORY"
          description="The ML service is optional and does not replace the deterministic trust layers."
        />
        <CardContent>
          {isLoading ? (
            <Skeleton className="h-16 w-full" />
          ) : isError ? (
            <div role="status" className="border border-hairline bg-surface p-4 text-sm font-semibold text-ink">
              ML service not connected - no model results available
            </div>
          ) : data ? (
            <div role="status" className="border border-hairline bg-surface p-4 text-sm font-semibold text-ink">
              ML service connected - no model results are displayed
            </div>
          ) : null}
        </CardContent>
      </Card>

      <Card>
        <CardHeader
          title="PLANNED APPROACH"
          description="A future advisory model may help identify patterns across sensor readings."
        />
        <CardContent>
          <p className="text-sm text-muted leading-relaxed">
            The planned approach is to study baseline voltage, noise, equipment-response slope, gas signal and equipment power together. Any future model would be advisory; the deterministic trust layers remain responsible for system decisions.
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
