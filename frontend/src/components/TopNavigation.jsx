const menuItems = [
  { label: 'Tổng quan', target: '#overview' },
  { label: 'Monitor', target: '#monitoring' },
  { label: 'Lịch sử', target: '#history' },
  { label: 'Nhập dữ liệu', target: '#add-reading' },
  { label: 'Hệ thống', target: '#system-status' },
];

export function TopNavigation() {
  return (
    <header className="main-navigation">
      <a className="navigation-brand" href="#overview" aria-label="Về đầu trang">
        <span className="brand-icon">!</span>
        <span>
          <strong>Disaster Warning Station</strong>
          <small>IoT · AI · Cloud</small>
        </span>
      </a>

      <nav className="navigation-links" aria-label="Menu chính">
        {menuItems.map((item) => (
          <a key={item.target} href={item.target}>
            {item.label}
          </a>
        ))}
      </nav>

      <span className="demo-label">DEMO MODE</span>
    </header>
  );
}
