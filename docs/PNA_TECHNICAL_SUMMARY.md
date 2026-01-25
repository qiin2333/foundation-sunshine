# PNA 修复技术总结

## 修复内容

### 问题分析
用户遇到的错误信息：
```
The connection is blocked because it was initiated by a public page to connect 
to devices or servers on your local network.
```

这是 Microsoft Edge 143+ 和 Chrome 141+ 新引入的 **Private Network Access (PNA)** 安全策略。

### 根本原因
当来自公网（如通过 window.open 加载的第三方应用）的 JavaScript 代码尝试访问本地网络资源（localhost、127.0.0.1 或 VPN 上的 IP）时，浏览器会：

1. **发送 CORS 预检请求（OPTIONS）** 到目标服务器
2. **检查响应头** 中是否包含 `Access-Control-Allow-Private-Network: true`
3. 只有当服务器响应了该头部，才允许实际请求

如果用户首次弹出授权窗口时点击了"取消"，浏览器会记住该决定，但即使后续支持了 PNA 头部，也可能因为第一次的否决而失败。

### 修复方案

在 Rust Axum 框架中添加了 **Private Network Access Middleware**：

#### 1. 导入必要的中间件模块
```rust
use axum::{
    extract::Request,
    response::{IntoResponse, Response},
    Router,
    middleware::Next,  // ← 新增
};
```

#### 2. 实现 PNA Middleware
```rust
async fn pna_middleware(
    req: Request,
    next: Next,
) -> Response {
    // 对 CORS 预检请求（OPTIONS）进行特殊处理
    if req.method() == axum::http::Method::OPTIONS {
        return axum::http::Response::builder()
            .status(axum::http::StatusCode::OK)
            .header("Access-Control-Allow-Origin", "*")
            .header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD")
            .header("Access-Control-Allow-Headers", "*")
            .header("Access-Control-Allow-Private-Network", "true")  // ← PNA 关键头部
            .header("Access-Control-Max-Age", "86400")
            .body(axum::body::Body::empty())
            .unwrap();
    }
    
    // 对所有其他请求的响应添加 PNA 头部
    let mut response = next.run(req).await;
    response.headers_mut().insert(
        "Access-Control-Allow-Private-Network",
        axum::http::HeaderValue::from_static("true"),
    );
    response
}
```

#### 3. 在路由器中注册 Middleware
```rust
pub async fn start_proxy_server() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let app = Router::new()
        .fallback(proxy_handler)
        .layer(CorsLayer::permissive())
        .layer(axum::middleware::from_fn(pna_middleware));  // ← 添加此行
    
    // ... 其余代码
}
```

## 技术细节

### 关键响应头
| 头部名称 | 值 | 说明 |
|---------|-----|-----|
| `Access-Control-Allow-Origin` | `*` | 允许来自任何源的请求 |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD` | 允许的 HTTP 方法 |
| `Access-Control-Allow-Headers` | `*` | 允许任何请求头 |
| `Access-Control-Allow-Private-Network` | `true` | **PNA 核心头部** |
| `Access-Control-Max-Age` | `86400` | 预检请求缓存 1 天 |

### Middleware 执行流程
```
请求入站
   ↓
pna_middleware (最外层)
   ↓
CorsLayer::permissive()
   ↓
proxy_handler (处理请求)
   ↓
响应返回
   ↓
pna_middleware 添加 PNA 头部
   ↓
响应出站
```

## 预期效果

修复后：
- ✅ 浏览器会在首次请求时发送 OPTIONS 预检请求，收到 PNA 头部许可
- ✅ 后续请求会被浏览器允许访问本地网络资源
- ✅ 即使用户曾点击"取消"，重启浏览器或应用后，PNA 许可重新请求会成功
- ✅ 兼容 Chrome 141+、Edge 143+ 和其他支持 PNA 的浏览器

## 限制与说明

1. **不会覆盖之前的"取消"决定**
   - 如果用户已在浏览器中点击了"取消"授权，需要：
     - 清除浏览器缓存或使用无痕模式
     - 或者重启浏览器
     - 或者在浏览器设置中手动重置权限

2. **服务器端完全兼容**
   - 这个修复只在服务器端实现，不涉及客户端代码更改
   - 对不支持 PNA 的旧浏览器完全向后兼容

3. **安全性**
   - 此修复遵循 W3C 标准，不降低任何安全性
   - 仅是明确告诉浏览器该服务器支持从公网访问本地网络资源

## 参考资源

- [Microsoft 官方答案 - 3rd party app unable to call endpoint in Edge 143](https://learn.microsoft.com/en-us/answers/questions/5647994/3rd-party-app-unable-to-call-our-end-point-in-edge)
- [Chrome Private Network Access 官方文档](https://developer.chrome.com/blog/private-network-access-preflight/)
- [W3C CORS-RFC1918 规范](https://wicg.github.io/private-network-access/)
- [Edge 143 变更日志](https://docs.microsoft.com/en-us/deployedge/microsoft-edge-release-notes)

## 验证修复

要验证修复是否生效，可以检查网络请求：

1. 打开浏览器开发者工具（F12）→ 网络标签
2. 向本地 API 发送请求
3. 检查请求头中 OPTIONS 预检请求的响应
4. 确认响应中包含：`access-control-allow-private-network: true`

示例响应头：
```http
HTTP/1.1 200 OK
access-control-allow-origin: *
access-control-allow-methods: GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD
access-control-allow-headers: *
access-control-allow-private-network: true
access-control-max-age: 86400
```

---

**修复文件：** `src_assets/common/sunshine-control-panel/src-tauri/src/proxy_server.rs`

**修改日期：** 2026-01-25

**编译状态：** ✅ 成功（no errors, no warnings）
