# Frontend

React + Vite + JavaScript dashboard cho giai đoạn web + database.

Frontend chỉ đọc dữ liệu qua FastAPI, không chứa dữ liệu mock và không kết nối trực tiếp PostgreSQL. Biểu mẫu nhập tay gọi `POST /api/readings` để kiểm chứng luồng ghi database.

```powershell
npm install
npm run dev
```

Frontend bắt buộc đọc `VITE_API_BASE_URL` từ file `.env` ở thư mục gốc repository. Nếu thiếu biến này, ứng dụng sẽ báo lỗi cấu hình thay vì tự dùng một URL dự phòng.

```powershell
npm run build
```

Source React dùng `.jsx`; service và cấu hình dùng `.js`. Frontend không còn phụ thuộc TypeScript hoặc các package `@types/*`.
