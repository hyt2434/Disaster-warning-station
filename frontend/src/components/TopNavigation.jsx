const menuItems = [
  { label: 'Tổng quan', target: '#overview' },
  { label: 'Giám sát · F1/F3/F6', target: '#monitoring' },
  { label: 'Điều khiển · F2', target: '#device-control' },
  { label: 'Lịch sử · F4', target: '#history' },
  { label: 'Dự đoán · F5', target: '#prediction' },
  { label: 'Rung/nghiêng · F7', target: '#f7-monitoring' },
  { label: 'Hệ thống · F8', target: '#system-status' },
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
