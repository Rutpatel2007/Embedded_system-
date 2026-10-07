import React from 'react';
import { cn } from '../../lib/utils';

export interface StatProps extends React.HTMLAttributes<HTMLDivElement> {
  label: string;
  value: React.ReactNode;
  unit?: string;
  subtext?: React.ReactNode;
  statusBadge?: React.ReactNode;
}

export function Stat({
  label,
  value,
  unit,
  subtext,
  statusBadge,
  className,
  ...props
}: StatProps) {
  return (
    <div
      className={cn(
        'p-4 border border-hairline bg-surface-elevated flex flex-col justify-between',
        className
      )}
      {...props}
    >
      <div className="flex items-center justify-between gap-2 mb-2">
        <span className="text-xs uppercase tracking-wider font-semibold text-muted">
          {label}
        </span>
        {statusBadge && <div>{statusBadge}</div>}
      </div>

      <div className="flex items-baseline gap-1.5 my-1">
        <div className="text-2xl font-semibold text-ink tracking-tight font-mono-num">
          {value}
        </div>
        {unit && (
          <span className="text-xs text-muted font-normal tracking-normal">
            {unit}
          </span>
        )}
      </div>

      {subtext && (
        <div className="text-xs text-muted mt-1 leading-snug">
          {subtext}
        </div>
      )}
    </div>
  );
}
