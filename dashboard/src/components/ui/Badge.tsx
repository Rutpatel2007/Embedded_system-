import React from 'react';
import { cn } from '../../lib/utils';

export type BadgeVariant = 'neutral' | 'verified' | 'warning' | 'critical';

export interface BadgeProps extends React.HTMLAttributes<HTMLSpanElement> {
  variant?: BadgeVariant;
  size?: 'sm' | 'md';
}

export function Badge({
  variant = 'neutral',
  size = 'md',
  className,
  children,
  ...props
}: BadgeProps) {
  const variantStyles: Record<BadgeVariant, string> = {
    neutral: 'bg-surface text-muted border-hairline',
    verified: 'bg-teal-subtle text-teal border-teal/30',
    warning: 'bg-ochre-subtle text-ochre border-ochre/30',
    critical: 'bg-brick-subtle text-brick border-brick/30',
  };

  const sizeStyles = {
    sm: 'text-[11px] px-1.5 py-0.5 tracking-tight',
    md: 'text-xs px-2 py-0.5 tracking-normal',
  };

  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 font-medium border font-mono-num rounded-none',
        variantStyles[variant],
        sizeStyles[size],
        className
      )}
      {...props}
    >
      <span className={cn(
        'w-1.5 h-1.5 rounded-full shrink-0',
        variant === 'neutral' && 'bg-muted/40',
        variant === 'verified' && 'bg-teal',
        variant === 'warning' && 'bg-ochre',
        variant === 'critical' && 'bg-brick',
      )} />
      {children}
    </span>
  );
}
