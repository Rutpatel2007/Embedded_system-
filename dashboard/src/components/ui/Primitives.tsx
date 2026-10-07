import React from 'react';
import { cn } from '../../lib/utils';

export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'primary' | 'secondary' | 'outline' | 'danger';
  size?: 'sm' | 'md' | 'lg';
  isLoading?: boolean;
}

export function Button({
  variant = 'secondary',
  size = 'md',
  isLoading = false,
  className,
  disabled,
  children,
  ...props
}: ButtonProps) {
  const variantStyles = {
    primary: 'bg-teal text-white hover:bg-teal/90 border border-teal',
    secondary: 'bg-surface text-ink hover:bg-surface/80 border border-hairline',
    outline: 'bg-transparent text-ink hover:bg-surface border border-hairline',
    danger: 'bg-brick text-white hover:bg-brick/90 border border-brick',
  };

  const sizeStyles = {
    sm: 'text-xs px-2.5 py-1.5 h-7',
    md: 'text-xs px-3.5 py-2 h-9',
    lg: 'text-sm px-4 py-2.5 h-10',
  };

  return (
    <button
      className={cn(
        'inline-flex items-center justify-center font-medium transition-colors duration-150 cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed select-none rounded-none',
        variantStyles[variant],
        sizeStyles[size],
        className
      )}
      disabled={disabled || isLoading}
      {...props}
    >
      {isLoading && (
        <svg
          className="animate-spin -ml-0.5 mr-2 h-3.5 w-3.5"
          xmlns="http://www.w3.org/2000/svg"
          fill="none"
          viewBox="0 0 24 24"
        >
          <circle
            className="opacity-25"
            cx="12"
            cy="12"
            r="10"
            stroke="currentColor"
            strokeWidth="4"
          />
          <path
            className="opacity-75"
            fill="currentColor"
            d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"
          />
        </svg>
      )}
      {children}
    </button>
  );
}

export function Skeleton({
  className,
  ...props
}: React.HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn('animate-pulse bg-surface/80 border border-hairline/40', className)}
      {...props}
    />
  );
}

export interface EmptyStateProps extends React.HTMLAttributes<HTMLDivElement> {
  title: string;
  description?: string;
  action?: React.ReactNode;
}

export function EmptyState({
  title,
  description,
  action,
  className,
  ...props
}: EmptyStateProps) {
  return (
    <div
      className={cn(
        'p-8 border border-dashed border-hairline text-center flex flex-col items-center justify-center',
        className
      )}
      {...props}
    >
      <h4 className="text-sm font-semibold text-ink">{title}</h4>
      {description && (
        <p className="text-xs text-muted max-w-sm mt-1">{description}</p>
      )}
      {action && <div className="mt-4">{action}</div>}
    </div>
  );
}
