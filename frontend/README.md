# Frontend

React + Vite + TypeScript dashboard cho giai đoạn web + database.

Frontend chỉ đọc dữ liệu qua FastAPI, không chứa dữ liệu mock và không kết nối trực tiếp PostgreSQL. Biểu mẫu nhập tay gọi `POST /api/readings` để kiểm chứng luồng ghi database.

```powershell
npm install
npm run dev
```

API mặc định: `http://localhost:8000`. Frontend đọc `VITE_API_BASE_URL` từ file `.env` ở thư mục gốc repository.

```powershell
npm run build
```
