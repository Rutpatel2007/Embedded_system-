import { describe, it, expect } from 'vitest';
import '@testing-library/jest-dom/vitest';
import { render, screen } from '@testing-library/react';
import { Badge } from './Badge';
import { Stat } from './Stat';

describe('UI status and verdict primitives', () => {
  it('renders verified badge with correct text', () => {
    render(<Badge variant="verified">CHAIN VERIFIED</Badge>);
    const badge = screen.getByText('CHAIN VERIFIED');
    expect(badge).toBeInTheDocument();
  });

  it('renders critical badge for compromised chain', () => {
    render(<Badge variant="critical">TAMPERED</Badge>);
    const badge = screen.getByText('TAMPERED');
    expect(badge).toBeInTheDocument();
  });

  it('renders stat component with numerical value and subtext', () => {
    render(
      <Stat
        label="Chain Integrity"
        value="VERIFIED"
        subtext="600 blocks checked"
        statusBadge={<Badge variant="verified">0 FAULTS</Badge>}
      />
    );
    expect(screen.getByText('Chain Integrity')).toBeInTheDocument();
    expect(screen.getByText('VERIFIED')).toBeInTheDocument();
    expect(screen.getByText('600 blocks checked')).toBeInTheDocument();
    expect(screen.getByText('0 FAULTS')).toBeInTheDocument();
  });
});
