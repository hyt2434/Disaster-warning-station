const menuItems = [
  { label: 'Tổng quan', target: '#overview' },
  { label: 'Cảm biến', target: '#monitoring' },
  { label: 'Còi', target: '#device-control' },
  { label: 'Lịch sử', target: '#history' },
  { label: 'Dự đoán', target: '#prediction' },
  { label: 'F7', target: '#f7-monitoring' },
  { label: 'Kết nối', target: '#system-status' },
];

export function TopNavigation() {
  return (
    <header className="main-navigation">
      <a className="navigation-brand" href="#overview" aria-label="Về đầu trang">
        <span className="brand-icon">!</span>
        <span>
          <strong>Disaster Warning Station</strong>
        </span>
      </a>

      <nav className="navigation-links" aria-label="Menu chính">
        {menuItems.map((item) => (
          <a key={item.target} href={item.target}>
            {item.label}
          </a>
        ))}
      </nav>
    </header>
  );
}
