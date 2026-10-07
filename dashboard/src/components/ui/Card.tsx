import React from 'react';
import { cn } from '../../lib/utils';

export interface CardProps extends React.HTMLAttributes<HTMLDivElement> {
  variant?: 'default' | 'elevated' | 'subtle';
}

export function Card({ className, variant = 'default', children, ...props }: CardProps) {
  return (
    <div
      className={cn(
        'border border-hairline bg-surface-elevated transition-colors duration-150',
        variant === 'elevated' && 'shadow-xs',
        variant === 'subtle' && 'bg-surface/50',
        className
      )}
      {...props}
    >
      {children}
    </div>
  );
}

export interface CardHeaderProps extends React.HTMLAttributes<HTMLDivElement> {
  title?: string;
  description?: string;
  action?: React.ReactNode;
}

export function CardHeader({ title, description, action, className, children, ...props }: CardHeaderProps) {
  return (
    <div className={cn('p-4 border-b border-hairline flex items-center justify-between gap-4', className)} {...props}>
      <div>
        {title && <h3 className="text-sm font-semibold tracking-tight text-ink">{title}</h3>}
        {description && <p className="text-xs text-muted mt-0.5">{description}</p>}
        {children}
      </div>
      {action && <div className="shrink-0">{action}</div>}
    </div>
  );
}

export function CardContent({ className, children, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  return (
    <div className={cn('p-4', className)} {...props}>
      {children}
    </div>
  );
}
