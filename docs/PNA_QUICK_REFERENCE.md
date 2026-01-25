# PNA 修复快速参考

## 问题
```
The connection is blocked because it was initiated by a public page to connect 
to devices or servers on your local network.
```

## 原因
Microsoft Edge 143+ 需要服务器在响应 CORS 预检请求时包含 `Access-Control-Allow-Private-Network: true` 头部，才能允许来自公网的请求访问本地网络。

## 解决方案
在 [proxy_server.rs](../src_assets/common/sunshine-control-panel/src-tauri/src/proxy_server.rs) 中添加 PNA Middleware。

### 修改内容概览

| 文件 | 修改说明 |
|------|--------|
| 代理服务器导入 | 添加 `middleware::Next` |
| `pna_middleware()` 函数 | 新增 Middleware 处理 OPTIONS 请求和添加 PNA 头部 |
| `start_proxy_server()` | 在路由器中注册 `.layer(axum::middleware::from_fn(pna_middleware))` |

## 验证方式

### 方式1：使用浏览器开发者工具
1. 打开应用，按 F12 打开开发者工具
2. 选择"网络"标签
3. 发送任何本地 API 请求
4. 找到 OPTIONS 预检请求，检查响应头
5. 确认包含：`access-control-allow-private-network: true`

### 方式2：使用 curl 命令
```bash
curl -i -X OPTIONS http://127.0.0.1:48081/api/test \
  -H "Access-Control-Request-Method: GET" \
  -H "Access-Control-Request-Headers: content-type" \
  -H "Origin: https://example.com"
```

预期响应包含：
```
access-control-allow-private-network: true
access-control-allow-origin: *
access-control-allow-methods: GET, POST, PUT, DELETE, OPTIONS, PATCH, HEAD
access-control-allow-headers: *
```

### 方式3：实际测试
1. 重启应用
2. 在浏览器中打开应用
3. 如果首次提示授权，点击"允许"
4. 应用应能正常访问本地 API

## 如果仍有问题

### 场景1：浏览器仍然提示"取消"授权
**原因：** 浏览器缓存了之前的"取消"决定

**解决方案：**
- 清除浏览器缓存（Ctrl+Shift+Delete）
- 或使用无痕模式重新打开
- 或尝试其他浏览器
- 或重启计算机

### 场景2：还是看到 PNA 错误
**检查清单：**
- [ ] 代理服务器是否正确重启？
- [ ] 确认 cargo check 通过（无编译错误）
- [ ] 检查浏览器网络请求中 OPTIONS 响应是否包含 PNA 头部
- [ ] 确保访问的是 http://127.0.0.1 或 localhost（不是其他 IP）
- [ ] 尝试使用最新版本的 Edge 或 Chrome

### 场景3：只有特定用户有问题
**可能原因：**
- 用户浏览器中有缓存的"取消"决定
- 用户使用的是旧版本浏览器（Chrome <141, Edge <143）

**解决方案：**
- 升级浏览器到最新版本
- 清除浏览器数据和权限设置

## 额外资源

- 完整技术说明：[PNA_TECHNICAL_SUMMARY.md](./PNA_TECHNICAL_SUMMARY.md)
- 详细修复说明：[PNA_FIX_EXPLANATION.md](./PNA_FIX_EXPLANATION.md)
- Microsoft 官方答案：https://learn.microsoft.com/en-us/answers/questions/5647994/3rd-party-app-unable-to-call-our-end-point-in-edge

## 对用户的说明

**告知用户的内容：**

> 我们已修复了一个与 Microsoft Edge 143+ 的兼容性问题。
>
> 如果您在首次打开应用时不小心点击了"取消"授权，现在只需：
> 1. 清除浏览器缓存（Ctrl+Shift+Delete）
> 2. 或使用无痕模式
> 3. 或直接重启浏览器
>
> 应用即可正常使用！

---

**修复完成日期：** 2026-01-25  
**编译状态：** ✅ 成功  
**兼容浏览器：** Edge 143+, Chrome 141+
