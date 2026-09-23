// ================= 缓速窗口的"凸面镜"特效 =================
//
// 做法(不碰游戏代码, 只挂一个 vtable):
//   1) 用它自己的 d3d11/dxgi 建一个"假设备 + 假交换链", 读出 IDXGISwapChain
//      的 vtable 地址(Present 是第 8 个槽)。同一个 dxgi.dll 里所有交换链共用这张
//      vtable, 所以把它改掉 = 游戏自己的交换链也被挂上。之后假对象全部释放。
//   2) 每次 Present: 如果"缓速窗口"正在跑, 把后台缓冲 CopyResource 到自己一张纹理,
//      再用全屏三角形贴回去 —— 像素着色器做桶形畸变(凸面镜) + 边缘压暗 + 冷色偏移。
//   3) 窗口一结束就什么都不做, 直接调 Present 原函数(零开销)。
//
// 安全:
//   - 任何一步失败 -> fx_dead=1, 永久关闭, 只留一行日志
//   - 只在 FX_ACTIVE() 为真时才碰 D3D
//   - 资源懒建; 后台缓冲尺寸/格式变了就重建
//
// 使用方必须先定义:
//   FX_LOG(fmt, ...)   打日志
//   FX_ACTIVE()        1 = 现在要画
#ifndef FX_GLOW_H
#define FX_GLOW_H

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#ifndef FX_LOG
#define FX_LOG(...) do { } while (0)
#endif
#ifndef FX_ACTIVE
#define FX_ACTIVE() 1
#endif
#ifndef FX_TICK
#define FX_TICK() do { } while (0)
#endif

// ---- 自己的 IID(不去 link uuid 库) ----
static const GUID FX_IID_Tex2D    = {0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};
static const GUID FX_IID_Dev11    = {0xdb6f6ddb,0xac77,0x4e88,{0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40}};
static const GUID FX_IID_Factory1 = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};

typedef HRESULT (WINAPI *FX_D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                          void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *FX_CreateDXGIFactory1_t)(REFIID, void**);
typedef HRESULT (WINAPI *FX_D3D11CreateDevice_t)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                 const D3D_FEATURE_LEVEL*, UINT, UINT,
                                                 ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
typedef HRESULT (STDMETHODCALLTYPE *FX_Present_t)(IDXGISwapChain*, UINT, UINT);

static volatile LONG fx_seen   = 0;    // Present 被调用次数
static volatile LONG fx_draws  = 0;    // 真正画了特效的次数
static volatile LONG fx_dead   = 0;    // 出错 -> 永久关
static volatile LONG fx_ready  = 0;    // 纹理/管线已建好
static volatile LONG fx_on     = 1;    // 开关(1=开)

static void*  fx_presentReal = NULL;
static ID3D11Device*             fx_dev = NULL;
static ID3D11DeviceContext*      fx_ctx = NULL;
static ID3D11Texture2D*          fx_tex = NULL;
static ID3D11ShaderResourceView* fx_srv = NULL;
static ID3D11RenderTargetView*   fx_rtv = NULL;
static ID3D11VertexShader*       fx_vs = NULL;
static ID3D11PixelShader*        fx_ps = NULL;
static ID3D11SamplerState*       fx_smp = NULL;
static ID3D11Buffer*             fx_cb = NULL;
static ID3D11RasterizerState*    fx_rs = NULL;
static UINT        fx_w = 0, fx_h = 0;
static DXGI_FORMAT fx_fmt = DXGI_FORMAT_UNKNOWN;
// ★v100: 用户"还是太夸张, 一直变眼睛晃" —— 再砍到几乎感觉不到的程度
static float       fx_zoom  = 0.006f;     // 镜头特写: 只推近 0.6%
static float       fx_blur  = 0.05f;      // 从中心向四周拉长: 很轻
static float       fx_dark  = 0.0f;       // 不压暗
static volatile float fx_fade = 1.0f;     // "出现进度" 0..1(宿主每帧更新, 从中心往外长)

static const char* FX_HLSL =
"Texture2D t0 : register(t0);\n"
"SamplerState s0 : register(s0);\n"
"cbuffer P : register(b0) { float g_zoom; float g_blur; float g_dark; float g_k; };\n"
"struct O { float4 p : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"O VS(uint id : SV_VertexID) {\n"
"    O o;\n"
"    float2 q = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);\n"
"    o.p  = float4(q, 0.0, 1.0);\n"
"    o.uv = float2((q.x + 1.0) * 0.5, 1.0 - (q.y + 1.0) * 0.5);\n"
"    return o;\n"
"}\n"
"float4 PS(O i) : SV_TARGET {\n"
"    float2 c  = i.uv - 0.5;\n"
"    float  r  = length(c);\n"
"    float  rn = saturate(r * 1.42);                 // 0=画面中心, 1=四个角\n"
"    float2 dir = (r > 0.0001) ? (c / r) : float2(0, 0);\n"
"    float  k  = saturate(g_k);\n"
"    // (1) 镜头特写: 整体推近一点\n"
"    float2 uv0 = 0.5 + c * (1.0 - g_zoom * k);\n"
"    // (2) 从中心向四周拉长: 沿半径方向多点采样 —— 越靠边拉得越长,\n"
"    //     而且长出来的过程是从中心往外扩散(kk 里带 -rn)\n"
"    float  kk  = saturate(k * 1.8 - rn);\n"
"    float  amt = g_blur * kk * (0.25 + 0.75 * rn);\n"
"    float3 acc = 0;\n"
"    for (int t = 0; t < 8; t++) {\n"
"        float f = (float)t / 7.0;\n"
"        float2 suv = 0.5 + (uv0 - 0.5) * (1.0 - amt * f);\n"
"        acc += t0.Sample(s0, suv).rgb;\n"
"    }\n"
"    acc /= 8.0;\n"
"    // (3) 边缘压暗一点, 把注意力收到中间的交锋上\n"
"    float dark = 1.0 - g_dark * k * rn * rn;\n"
"    float3 fin = acc * dark;\n"
"    fin *= lerp(float3(1,1,1), float3(0.96, 0.98, 1.03), k * 0.2);   // 几乎看不出冷色\n"
"    return float4(fin, 1.0);\n"
"}\n";

static HRESULT STDMETHODCALLTYPE fx_Present(IDXGISwapChain* sc, UINT sync, UINT flags);

// 用假设备读出 IDXGISwapChain 的 vtable, 并把 Present 槽换成我们自己的
static bool fx_install(void)
{
    HMODULE hdxgi = LoadLibraryA("dxgi.dll");
    HMODULE h11   = LoadLibraryA("d3d11.dll");
    FX_CreateDXGIFactory1_t pCreateFactory1 = NULL;
    FX_D3D11CreateDevice_t  pCreateDevice   = NULL;
    IDXGIFactory1* fac = NULL;
    ID3D11Device*  dev = NULL;
    ID3D11DeviceContext* ctx = NULL;
    IDXGISwapChain* sc = NULL;
    HWND wnd = NULL;
    WNDCLASSEXA wc;
    char cls[64];
    bool ok = false;
    DWORD oldp = 0;
    void** vt = NULL;

    if (hdxgi == NULL || h11 == NULL) { FX_LOG("FX 初始化失败: 找不到 dxgi/d3d11\n"); return false; }
    pCreateFactory1 = (FX_CreateDXGIFactory1_t)GetProcAddress(hdxgi, "CreateDXGIFactory1");
    pCreateDevice   = (FX_D3D11CreateDevice_t) GetProcAddress(h11,   "D3D11CreateDevice");
    if (pCreateFactory1 == NULL || pCreateDevice == NULL) { FX_LOG("FX 初始化失败: 找不到导出函数\n"); return false; }

    sprintf(cls, "fxcls%u", (unsigned)GetCurrentProcessId());
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    RegisterClassExA(&wc);
    wnd = CreateWindowExA(0, cls, "fx", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (wnd == NULL) { FX_LOG("FX 初始化失败: 建不出隐藏窗口(err=%lu)\n", GetLastError()); return false; }

    if (FAILED(pCreateFactory1(FX_IID_Factory1, (void**)&fac)) || fac == NULL)
    { FX_LOG("FX 初始化失败: CreateDXGIFactory1\n"); goto done; }
    if (FAILED(pCreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                             D3D11_SDK_VERSION, &dev, NULL, &ctx)) || dev == NULL)
    { FX_LOG("FX 初始化失败: D3D11CreateDevice\n"); goto done; }
    {
        DXGI_SWAP_CHAIN_DESC sd;
        memset(&sd, 0, sizeof(sd));
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 64;
        sd.BufferDesc.Height = 64;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = wnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        if (FAILED(fac->CreateSwapChain((IUnknown*)dev, &sd, &sc)) || sc == NULL)
        { FX_LOG("FX 初始化失败: CreateSwapChain\n"); goto done; }
    }
    vt = *(void***)sc;
    if (vt == NULL || vt[8] == NULL) { FX_LOG("FX 初始化失败: vtable 不对\n"); goto done; }
    fx_presentReal = vt[8];
    if (!VirtualProtect(&vt[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldp))
    { FX_LOG("FX 初始化失败: VirtualProtect\n"); goto done; }
    vt[8] = (void*)&fx_Present;
    VirtualProtect(&vt[8], sizeof(void*), oldp, &oldp);
    FlushInstructionCache(GetCurrentProcess(), &vt[8], sizeof(void*));
    ok = true;
    {
        HMODULE owner = NULL;
        char path[MAX_PATH] = {0};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)fx_presentReal, &owner);
        if (owner) GetModuleFileNameA(owner, path, MAX_PATH);
        FX_LOG("FX Present 已挂上: 原函数=%p (%s) vtable=%p\n",
               fx_presentReal, path[0] ? path : "?", (void*)vt);
    }

done:
    if (sc)  sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (fac) fac->Release();
    if (wnd) DestroyWindow(wnd);
    UnregisterClassA(cls, wc.hInstance);
    return ok;
}

// ---------------------------------------------------------------- 建管线
static bool fx_build(IDXGISwapChain* sc)
{
    HMODULE hc = NULL;
    FX_D3DCompile_t pCompile = NULL;
    ID3DBlob* vsb = NULL;
    ID3DBlob* psb = NULL;
    ID3DBlob* err = NULL;
    static int fx_built = 0;
    D3D11_SAMPLER_DESC sd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_BUFFER_DESC bd;

    if (fx_built) return true;
    if (fx_dev == NULL)
    {
        ID3D11Device* dev = NULL;
        if (FAILED(sc->GetDevice(FX_IID_Dev11, (void**)&dev)) || dev == NULL)
        { FX_LOG("FX: GetDevice 失败\n"); return false; }
        fx_dev = dev;
        dev->GetImmediateContext(&fx_ctx);
        if (fx_ctx == NULL) { FX_LOG("FX: GetImmediateContext 失败\n"); return false; }
    }

    hc = LoadLibraryA("d3dcompiler_47.dll");
    if (hc == NULL) { FX_LOG("FX: 找不到 d3dcompiler_47.dll, 特效关闭\n"); return false; }
    pCompile = (FX_D3DCompile_t)GetProcAddress(hc, "D3DCompile");
    if (pCompile == NULL) { FX_LOG("FX: 找不到 D3DCompile\n"); return false; }

    if (FAILED(pCompile(FX_HLSL, strlen(FX_HLSL), NULL, NULL, NULL, "VS", "vs_4_0", 0, 0, &vsb, &err)))
    {
        FX_LOG("FX: VS 编译失败: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        return false;
    }
    if (FAILED(pCompile(FX_HLSL, strlen(FX_HLSL), NULL, NULL, NULL, "PS", "ps_4_0", 0, 0, &psb, &err)))
    {
        FX_LOG("FX: PS 编译失败: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        vsb->Release();
        return false;
    }
    if (FAILED(fx_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), NULL, &fx_vs)) || fx_vs == NULL)
    { FX_LOG("FX: CreateVertexShader 失败\n"); goto fail; }
    if (FAILED(fx_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &fx_ps)) || fx_ps == NULL)
    { FX_LOG("FX: CreatePixelShader 失败\n"); goto fail; }

    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(fx_dev->CreateSamplerState(&sd, &fx_smp))) { FX_LOG("FX: 采样器失败\n"); goto fail; }

    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(fx_dev->CreateRasterizerState(&rd, &fx_rs))) { FX_LOG("FX: 光栅状态失败\n"); goto fail; }

    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(fx_dev->CreateBuffer(&bd, NULL, &fx_cb))) { FX_LOG("FX: 常量缓冲失败\n"); goto fail; }

    vsb->Release(); psb->Release();
    fx_built = 1;
    FX_LOG("FX: 管线建好了(vs/ps/sampler/rs/cb)\n");
    return true;

fail:
    if (vsb) vsb->Release();
    if (psb) psb->Release();
    return false;
}

// ---------------------------------------------------------------- 每帧画
static void fx_draw(IDXGISwapChain* sc)
{
    ID3D11Texture2D* bb = NULL;
    D3D11_TEXTURE2D_DESC dd;
    D3D11_VIEWPORT vp;
    D3D11_MAPPED_SUBRESOURCE ms;
    float par[4];
    int needRecreate = 0;

    if (fx_dead || !fx_on || sc == NULL) return;
    if (!fx_build(sc)) { InterlockedExchange(&fx_dead, 1); return; }

    if (FAILED(sc->GetBuffer(0, FX_IID_Tex2D, (void**)&bb)) || bb == NULL) return;
    memset(&dd, 0, sizeof(dd));
    bb->GetDesc(&dd);
    if (dd.SampleDesc.Count > 1)
    {
        // 后台缓冲带 MSAA: 不能直接建普通 SRV(CopyResource 出来也一样), 直接认输
        FX_LOG("FX: 后台缓冲是 MSAA(Count=%u), 特效关闭\n", dd.SampleDesc.Count);
        bb->Release();
        InterlockedExchange(&fx_dead, 1);
        return;
    }
    if (fx_tex == NULL || dd.Width != fx_w || dd.Height != fx_h || dd.Format != fx_fmt) needRecreate = 1;

    if (needRecreate)
    {
        ID3D11Texture2D* tex = NULL;
        ID3D11ShaderResourceView* srv = NULL;
        ID3D11RenderTargetView* rtv = NULL;
        D3D11_TEXTURE2D_DESC td;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv;
        D3D11_RENDER_TARGET_VIEW_DESC rv;

        if (fx_srv) { fx_srv->Release(); fx_srv = NULL; }
        if (fx_rtv) { fx_rtv->Release(); fx_rtv = NULL; }
        if (fx_tex) { fx_tex->Release(); fx_tex = NULL; }

        memset(&td, 0, sizeof(td));
        td.Width = dd.Width; td.Height = dd.Height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = dd.Format; td.SampleDesc = dd.SampleDesc;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(fx_dev->CreateTexture2D(&td, NULL, &tex)) || tex == NULL)
        { FX_LOG("FX: CreateTexture2D 失败 (fmt=%d %ux%u)\n", (int)dd.Format, dd.Width, dd.Height); InterlockedExchange(&fx_dead, 1); bb->Release(); return; }
        memset(&sv, 0, sizeof(sv));
        sv.Format = dd.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        if (FAILED(fx_dev->CreateShaderResourceView(tex, &sv, &srv)) || srv == NULL)
        { FX_LOG("FX: CreateShaderResourceView 失败\n"); tex->Release(); InterlockedExchange(&fx_dead, 1); bb->Release(); return; }
        memset(&rv, 0, sizeof(rv));
        rv.Format = dd.Format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(fx_dev->CreateRenderTargetView(bb, &rv, &rtv)) || rtv == NULL)
        { FX_LOG("FX: CreateRenderTargetView 失败\n"); tex->Release(); srv->Release(); InterlockedExchange(&fx_dead, 1); bb->Release(); return; }
        fx_tex = tex; fx_srv = srv; fx_rtv = rtv;
        fx_w = dd.Width; fx_h = dd.Height; fx_fmt = dd.Format;
        InterlockedExchange(&fx_ready, 1);
        FX_LOG("FX: 后台缓冲纹理建好 %ux%u fmt=%d\n", fx_w, fx_h, (int)fx_fmt);
    }

    // 把当前画面拷进我们的纹理(这就是"照镜子"用的图)
    fx_ctx->CopyResource((ID3D11Resource*)fx_tex, (ID3D11Resource*)bb);
    bb->Release();

    par[0] = fx_zoom; par[1] = fx_blur; par[2] = fx_dark; par[3] = fx_fade;
    if (SUCCEEDED(fx_ctx->Map((ID3D11Resource*)fx_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        memcpy(ms.pData, par, sizeof(par));
        fx_ctx->Unmap((ID3D11Resource*)fx_cb, 0);
    }

    memset(&vp, 0, sizeof(vp));
    vp.Width = (FLOAT)fx_w; vp.Height = (FLOAT)fx_h; vp.MaxDepth = 1.0f;
    fx_ctx->OMSetRenderTargets(1, &fx_rtv, NULL);
    fx_ctx->RSSetViewports(1, &vp);
    fx_ctx->RSSetState(fx_rs);
    fx_ctx->RSSetScissorRects(0, NULL);
    fx_ctx->IASetInputLayout(NULL);
    fx_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    fx_ctx->VSSetShader(fx_vs, NULL, 0);
    fx_ctx->PSSetShader(fx_ps, NULL, 0);
    fx_ctx->PSSetConstantBuffers(0, 1, &fx_cb);
    fx_ctx->PSSetShaderResources(0, 1, &fx_srv);
    fx_ctx->PSSetSamplers(0, 1, &fx_smp);
    fx_ctx->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
    fx_ctx->OMSetDepthStencilState(NULL, 0);
    fx_ctx->Draw(3, 0);
    { ID3D11ShaderResourceView* none = NULL; fx_ctx->PSSetShaderResources(0, 1, &none); }
    fx_ctx->OMSetRenderTargets(0, NULL, NULL);
    InterlockedIncrement(&fx_draws);
}

static HRESULT STDMETHODCALLTYPE fx_Present(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    InterlockedIncrement(&fx_seen);
    FX_TICK();
    if (FX_ACTIVE())
    {
        if (!fx_dead && fx_on) fx_draw(sc);
    }
    return ((FX_Present_t)fx_presentReal)(sc, sync, flags);
}

#endif // FX_GLOW_H
