# Private Network Access (PNA) 修复说明

## 问题描述
用户在使用 Microsoft Edge 143+ 版本时，首次访问应用弹出授权窗口，如果误点"取消"，后续访问会遇到以下错误：
```
The connection is blocked because it was initiated by a public page to connect to devices or servers on your local network.
```

## 根本原因
Microsoft Edge 143+ 版本引入了更严格的 **Private Network Access (PNA)** 安全策略。根据 Chromium 最新安全标准（对应 Chrome 141-142 和 Edge 143），来自公网（public page）的请求无法直接访问本地网络或私有网络上的资源，除非：

1. 服务器端在 CORS 预检请求（OPTIONS）中响应特殊的 PNA 头部
2. 响应包含 `Access-Control-Allow-Private-Network: true` 头部

## 解决方案
在 [proxy_server.rs](../src_assets/common/sunshine-control-panel/src-tauri/src/proxy_server.rs) 中添加了 **PNA Middleware**，用于：

### 1. 处理 CORS 预检请求（OPTIONS）
当浏览器发送 OPTIONS 请求时，middleware 会返回完整的 CORS 和 PNA 头部：
```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD
Access-Control-Allow-Headers: *
Access-Control-Allow-Private-Network: true
Access-Control-Max-Age: 86400
```

### 2. 为所有响应添加 PNA 头部
对于非 OPTIONS 请求，middleware 会在响应中添加：
```http
Access-Control-Allow-Private-Network: true
```

## 代码实现
```rust
/// Private Network Access (PNA) Middleware
/// 根据 Microsoft Edge 143+ 的要求添加 PNA 支持头部
async fn pna_middleware(
    req: Request,
    next: Next,
) -> Response {
    // 检查是否是 OPTIONS 预检请求（CORS）
    if req.method() == axum::http::Method::OPTIONS {
        // 处理 CORS 预检请求，添加 PNA 支持
        let response = axum::http::Response::builder()
            .status(axum::http::StatusCode::OK)
            .header("Access-Control-Allow-Origin", "*")
            .header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD")
            .header("Access-Control-Allow-Headers", "*")
            .header("Access-Control-Allow-Private-Network", "true")
            .header("Access-Control-Max-Age", "86400")
            .body(axum::body::Body::empty())
            .unwrap();
        return response;
    }
    
    // 对于非 OPTIONS 请求，执行原有的处理器，然后在响应中添加 PNA 头部
    let mut response = next.run(req).await;
    
    // 添加 PNA 支持头部到所有响应
    let headers = response.headers_mut();
    headers.insert(
        "Access-Control-Allow-Private-Network",
        axum::http::HeaderValue::from_static("true"),
    );
    
    response
}
```

### Middleware 集成
```rust
pub async fn start_proxy_server() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let app = Router::new()
        .fallback(proxy_handler)
        .layer(CorsLayer::permissive())
        .layer(axum::middleware::from_fn(pna_middleware));  // ← 添加 PNA middleware
    // ...
}
```

## 预期效果
修复后，用户在以下场景不再会遇到 PNA 限制：
- ✅ 首次访问应用时允许授权
- ✅ 即使曾点击"取消"，后续访问也能正常工作（服务器端已支持 PNA）
- ✅ Microsoft Edge 143+ 中公网加载的应用可以访问本地网络资源
- ✅ 其他支持 PNA 的浏览器（Chrome 141+）也得到支持

## 参考资源
- [Microsoft 官方答案](https://learn.microsoft.com/en-us/answers/questions/5647994/3rd-party-app-unable-to-call-our-end-point-in-edge)
- [Chrome Private Network Access 提案](https://developer.chrome.com/blog/private-network-access-preflight/)
- [W3C CORS-RFC1918 规范](https://wicg.github.io/private-network-access/)

## 注意
- 此修复不需要用户更改浏览器设置
- 不涉及任何安全权限降低，仅是服务器端正确支持 PNA 标准
- 对已支持 PNA 的浏览器完全向后兼容
