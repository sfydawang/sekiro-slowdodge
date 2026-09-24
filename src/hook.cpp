// hook.dll ?? ??????????????????? MinHook?
//
// ???sekiro.exe+0xB6E6A0
//   [0x140B6E76D] call 0x140BD5CE0  ; IsInvincible(?? ChrIns)
//   [0x140B6E779] jne  0x140B6EA2F  ; ? ?? = ???????????????
//   [0x140B6E892] call 0x140BD4D40  ; ModifyHP(?? ChrIns, -??, ...)
//
// ?? ChrIns? ChrMan = [arg1+8] ; ChrIns = [[ChrMan+0x1FF8]+0x18]
//
// ???????16 ?? + 1 ? 7 ?? = 22 ???0xB6E6A0..0xB6E6B5?
//   48 8B C4 | 55 | 53 | 56 | 57 | 41 56 | 41 57 | 48 8D 68 B8 | 48 81 EC 18 01 00 00
//   ?????/??????? RIP ???? ? ???????
//
// ?????? "jmp" ???rsp ???????????????
// "mov rax,rsp" ?????????? rsp??? 5 ?????????

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <tlhelp32.h>

// ★★★ v131: 路径不再写死 —— 全部改成"跟着 DLL 走"(DLL 所在目录)。
//   原因: 发布给别人时 C:\Users\47297\... 根本不存在, 日志/配置会全废。
//   MODE.txt 额外做了一次回退: 优先找 DLL 同目录的 MODE.txt, 找不到就用桌面上的
//   (作者本机的习惯保持可用, 别人也能把 MODE.txt 放在 DLL 旁边)。
static char g_modDir[MAX_PATH] = {0};
static const char* mod_dir(void)
{
    if (g_modDir[0] == 0)
    {
        char p[MAX_PATH] = {0};
        HMODULE hm = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)&mod_dir, &hm);
        if (hm != NULL && GetModuleFileNameA(hm, p, MAX_PATH) > 0)
        {
            char* s = strrchr(p, '\\');
            if (s != NULL) *s = 0;
            lstrcpynA(g_modDir, p, MAX_PATH);
        }
    }
    return g_modDir;
}
#define PATH_FN(name, file) \
    static const char* name(void) { static char b[MAX_PATH]; sprintf(b, "%s\\" file, mod_dir()); return b; }
PATH_FN(P_LOG,  "hook.txt")
PATH_FN(P_TIME, "timeline.txt")
PATH_FN(P_CHAR, "chars.txt")
PATH_FN(P_SLOW, "slow.txt")
PATH_FN(P_WAV,  "assets\\fx_warp.wav")
static const char* P_MODE(void)
{
    static char b[MAX_PATH], t[MAX_PATH];
    sprintf(b, "%s\\MODE.txt", mod_dir());
    if (GetFileAttributesA(b) != INVALID_FILE_ATTRIBUTES) return b;
    if (GetEnvironmentVariableA("USERPROFILE", t, MAX_PATH) > 0)
    {
        sprintf(b, "%s\\Desktop\\MODE.txt", t);
        if (GetFileAttributesA(b) != INVALID_FILE_ATTRIBUTES) return b;
        sprintf(b, "%s\\MODE.txt", mod_dir());
    }
    return b;
}
#define LOGPATH  P_LOG()
#define MODEPATH P_MODE()
#define TIMEPATH P_TIME()
#define CHARPATH P_CHAR()

#define F_TARGET   0x00B6E6A0ULL
#define PROLOG_LEN 15
#define DUMP_BYTES 0x300
#define ATK_BYTES  0x240

static unsigned long long g_base = 0;
static volatile unsigned long long g_player = 0;

// ---------- ?????????????----------
#define MAX_CHR 48
static volatile unsigned long long g_pend[8];
static volatile unsigned long long g_pendCtr[8];              // ?? chr ???"??"??
static unsigned long long        g_ctrOf[MAX_CHR];            // ?????????
static volatile unsigned long long g_pendDummy;
static volatile LONG g_npend = 0;

struct ChrState {
    unsigned long long addr;
    int   hp, act;
    float x, y, z;
};
static ChrState      g_chr[MAX_CHR];
static volatile LONG g_nchr = 0;
static unsigned long long g_chrVt = 0;      // ???????? ChrIns ????

extern "C" {
void*             g_tramp = nullptr;
void*             g_tramp2 = nullptr;
unsigned long long g_targetMan = 0;
// ??????????(?????) ?? ? DIST ??? 250ms ??
static unsigned long long g_nearCtr = 0;
void  tgt_stub(void);
unsigned long long g_arg1 = 0, g_arg2 = 0, g_arg3 = 0;
void hook_c(void);
void hook_stub(void);
void*             g_tstramp = nullptr;
float             g_timeScale = 1.0f;
float ts_scale(void);
void  ts_stub(void);
LONG  g_tsCalls = 0;
float g_tsVal = 0.0f;
unsigned long long g_tsRsi = 0;
}

asm(
"  .text\n"
".globl tgt_stub\n"
".def tgt_stub; .scl 2; .type 32; .endef\n"
"tgt_stub:\n"
"  movq %rax, g_targetMan(%rip)\n"
"  jmp *g_tramp2(%rip)\n"
"  .text\n"
".globl ts_stub\n"
".def ts_stub; .scl 2; .type 32; .endef\n"
"ts_stub:\n"
"  pushfq\n"
"  pushq %rax\n"
"  pushq %rcx\n"
"  pushq %rdx\n"
"  pushq %r8\n"
"  pushq %r9\n"
"  pushq %r10\n"
"  pushq %r11\n"
"  movq %rsi, g_tsRsi(%rip)\n"
"  movss %xmm6, g_tsVal(%rip)\n"
"  call ts_scale\n"
"  mulss %xmm0, %xmm6\n"
"  addq $0x28, %rsp\n"
"  popq %r11\n"
"  popq %r10\n"
"  popq %r9\n"
"  popq %r8\n"
"  popq %rdx\n"
"  popq %rcx\n"
"  popq %rax\n"
"  popfq\n"
"  jmp *g_tstramp(%rip)\n"
".text\n"
".globl hook_stub\n"
".def hook_stub; .scl 2; .type 32; .endef\n"
"hook_stub:\n"
"  pushfq\n"
"  pushq %rax\n"
"  pushq %rcx\n"
"  pushq %rdx\n"
"  pushq %r8\n"
"  pushq %r9\n"
"  pushq %r10\n"
"  pushq %r11\n"
"  movq %rcx, g_arg1(%rip)\n"
"  movq %rdx, g_arg2(%rip)\n"
"  movq %r8,  g_arg3(%rip)\n"
"  subq $0x28, %rsp\n"
"  call hook_c\n"
"  addq $0x28, %rsp\n"
"  popq %r11\n"
"  popq %r10\n"
"  popq %r9\n"
"  popq %r8\n"
"  popq %rdx\n"
"  popq %rcx\n"
"  popq %rax\n"
"  popfq\n"
"  jmp *g_tramp(%rip)\n"
);

// ---------- ???? ----------
static volatile LONG g_srCalls = 0;      // safe_read ??????(????)
// v72: ????? ?? VirtualQuery ??????? 2~3ms, ???????
#define RD_RQ 64
typedef struct { unsigned long long b, e; DWORD t; } RdRegion;
static RdRegion g_rdq[RD_RQ];
static volatile LONG g_rdqUse = 0;
static double qms(void)
{
    static LARGE_INTEGER fq;
    LARGE_INTEGER c;
    if (fq.QuadPart == 0) QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)fq.QuadPart;
}
static bool safe_read(unsigned long long addr, void* out, size_t n)
{
    // ★★ v83 第三次改, 说明为什么:
    //   (1) 最早用 VirtualQuery: 这个进程地址区段极多, 一次 2~3ms, 把轮询线程拖到
    //       100~800ms 一轮 -> 采样全废。
    //   (2) v72 换成"区段缓存 + 直接 memcpy": 快, 但缓存有 1 秒有效期 —— 游戏把那
    //       段内存释放/改保护之后我们还在按缓存 memcpy, 就踩崩了。
    //       (11:51 那次崩溃: 出错模块 msvcrt.dll 偏移 0x7b18f = memcpy 访问违例)
    //   (3) 现在用 ReadProcessMemory 读自己进程: 永远不会崩, 一次约 1~2 微秒
    //       (比 VirtualQuery 快三个数量级), 我们每秒几百次读取完全够用。
    SIZE_T got = 0;
    InterlockedIncrement(&g_srCalls);
    if (addr < 0x10000ULL || addr > 0x7FFFFFFFFFFFULL) return false;
    if (n == 0) return true;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)addr, out, n, &got)) return false;
    return got == n;
}
static bool rd_ptr(unsigned long long a, unsigned long long* o) { return safe_read(a, o, 8); }
// ????: ?????, ??????; ????? VirtualProtect ????, ?????
// ???? ?? ?????/?????????
static bool safe_write(unsigned long long addr, const void* src, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD oldp = 0;
    if (addr < 0x10000ULL || addr > 0x7FFFFFFFFFFFULL) return false;
    if (VirtualQuery((LPCVOID)(uintptr_t)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    if (((unsigned long long)(uintptr_t)mbi.BaseAddress + mbi.RegionSize) - addr < n) return false;
    if (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
    {
        memcpy((void*)(uintptr_t)addr, src, n);
        return true;
    }
    if (!VirtualProtect((LPVOID)(uintptr_t)addr, n, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    memcpy((void*)(uintptr_t)addr, src, n);
    VirtualProtect((LPVOID)(uintptr_t)addr, n, oldp, &oldp);
    return true;
}
static bool rd_u32(unsigned long long a, int* o)                { return safe_read(a, o, 4); }
static bool rd_u8 (unsigned long long a, unsigned char* o)      { return safe_read(a, o, 1); }

static unsigned long long resolve_player(void)
{
    if (g_base == 0) return 0;
    unsigned long long p = 0, q = 0;
    if (!rd_ptr(g_base + 0x3D7A1E0ULL, &p) || p == 0) return 0;
    if (!rd_ptr(p + 0x88, &q) || q == 0) return 0;
    if (!rd_ptr(q + 0x1FF8, &p) || p == 0) return 0;
    if (!rd_ptr(p + 0x18, &q) || q == 0) return 0;
    return q;
}

// ---- ????????????????"??"????????? ChrIns ? ----
static unsigned long long ctr_of_man(unsigned long long man)
{
    unsigned long long m = 0, c = 0, ch = 0;
    if (man == 0) return 0;
    // ?????? +0x88??????? Man ??? +8 ?? ??????????
    if (rd_ptr(man + 0x88, &m) && m != 0 &&
        rd_ptr(m + 0x1FF8, &c) && c != 0 &&
        rd_ptr(c + 0x18, &ch) && ch != 0)
        return c;
    if (rd_ptr(man + 8, &m) && m != 0 &&
        rd_ptr(m + 0x1FF8, &c) && c != 0 &&
        rd_ptr(c + 0x18, &ch) && ch != 0)
        return c;
    return 0;
}
static unsigned long long ctr_of_player(void)
{
    unsigned long long wcm = 0;
    if (!rd_ptr(g_base + 0x3D7A1E0ULL, &wcm) || wcm == 0) return 0;
    return ctr_of_man(wcm);
}

// ??????"??? Man"??????: ???? Man+0x1FF8
// (TARGET ??????? rd_ptr(g_targetMan + 0x1FF8) ???, ???;
//  ? ctr_of_man ?? WorldChrMan ??, ??? Man ??? 0 ??
//  ??? v33 ? 18 ? HIT ????????)
static unsigned long long ctr_of_attack_man(unsigned long long man)
{
    unsigned long long c = 0, ch = 0, m = 0;
    if (man == 0) return 0;
    if ((man & 7) != 0) return 0;
    if (rd_ptr(man + 0x1FF8, &c) && (c & 7) == 0 && c != 0 &&
        rd_ptr(c + 0x18, &ch) && (ch & 7) == 0 && ch != 0)
        return c;
    if (rd_ptr(man + 0x88, &m) && (m & 7) == 0 && m != 0 &&
        rd_ptr(m + 0x1FF8, &c) && (c & 7) == 0 && c != 0 &&
        rd_ptr(c + 0x18, &ch) && (ch & 7) == 0 && ch != 0)
        return c;
    return 0;
}

static bool ctr_pos(unsigned long long ctr, float* p)
{
    unsigned long long o = 0;
    if (!rd_ptr(ctr + 0x68, &o) || o == 0) return false;
    return safe_read(o + 0x80, p, 12);
}
static unsigned long long player_chr(void)
{
    unsigned long long c = ctr_of_player(), ch = 0;
    if (c != 0) rd_ptr(c + 0x18, &ch);
    return ch;
}
static bool ctr_anim(unsigned long long ctr, char* out, int n)
{
    unsigned long long o = 0, s = 0;
    int i;
    out[0] = 0;
    if (!rd_ptr(ctr + 0x28, &o) || o == 0) return false;
    if (!rd_ptr(o + 0x878, &s) || s == 0) return false;
    for (i = 0; i < n - 1; i++)
    {
        unsigned char ch = 0;
        if (!safe_read(s + i, &ch, 1) || ch == 0) { out[i] = 0; return i > 0; }
        out[i] = (char)ch;
    }
    out[n - 1] = 0;
    return true;
}

// ================= ??????ID (?? CT ????) =================
//   ??  : [[[[WorldChrMan+0x88]+0x1FF8]+0x10]+0x20]   (4 Bytes)
//   ???NPC: [[[targetMan+0x1FF8]+0x10]+0x20]          (4 Bytes)
//   ???? +0x24 = "Length played [seconds]" (Float)
static int anim_of_ctr(unsigned long long ctr, float* lenOut)
{
    unsigned long long ob = 0;
    int id = -1;
    if (ctr == 0) return -1;
    if (!rd_ptr(ctr + 0x10, &ob) || ob == 0) return -1;
    rd_u32(ob + 0x20, &id);
    if (lenOut)
    {
        float L = 0.0f;
        if (safe_read(ob + 0x24, &L, 4)) *lenOut = L;
    }
    return id;
}

// ---- ????ID ???? (??? ~16ms ??, ???? 2 ?) ----
#define ANIMRING 128
static DWORD g_aT[ANIMRING];
static int   g_aV[ANIMRING];
static volatile LONG g_aN = 0;

static void anim_push(DWORD now, int id)
{
    LONG n = g_aN;
    g_aT[n % ANIMRING] = now;
    g_aV[n % ANIMRING] = id;
    InterlockedIncrement(&g_aN);
}
// backMs ????????ID
static int anim_at(DWORD now, DWORD backMs, DWORD* ageOut)
{
    LONG n = g_aN;
    int k;
    if (ageOut) *ageOut = 0;
    for (k = 1; k < ANIMRING && k <= n; k++)
    {
        int idx = (int)((n - k) % ANIMRING);
        DWORD t = g_aT[idx];
        if (now - t >= backMs) { if (ageOut) *ageOut = now - t; return g_aV[idx]; }
    }
    return -1;
}
// ???????????
static DWORD anim_stable_ms(DWORD now, int id)
{
    LONG n = g_aN;
    int k;
    DWORD t0 = now;
    if (id < 0) return 0;
    for (k = 1; k < ANIMRING && k <= n; k++)
    {
        int idx = (int)((n - k) % ANIMRING);
        if (g_aV[idx] != id) break;
        t0 = g_aT[idx];
    }
    return now - t0;
}

// ?????????:
//   fast : ??????, ?????"?????"(??????) ?? ? 16ms ???, ??
//   best : ????????? ?? ?????????
static unsigned long long enemy_ctr_fast(void)
{
    unsigned long long c = 0;
    if (g_targetMan != 0 && rd_ptr(g_targetMan + 0x1FF8, &c) && c != 0) return c;
    return g_nearCtr;
}

static unsigned long long enemy_ctr_best(void)
{
    unsigned long long t = 0, pc = 0;
    float p[3] = {0, 0, 0};
    bool haveP2 = false;
    if (g_targetMan != 0) rd_ptr(g_targetMan + 0x1FF8, &t);
    pc = ctr_of_player();
    if (pc != 0) haveP2 = ctr_pos(pc, p);
    if (g_nearCtr == 0 || g_nearCtr == t) return t;
    if (!haveP2) return (t != 0) ? t : g_nearCtr;
    {
        float a[3], b[3];
        float da = 1.0e18f, db = 1.0e18f;
        if (t != 0 && ctr_pos(t, a))
        { float dx = a[0]-p[0], dy = a[1]-p[1], dz = a[2]-p[2]; da = dx*dx + dy*dy + dz*dz; }
        if (ctr_pos(g_nearCtr, b))
        { float dx = b[0]-p[0], dy = b[1]-p[1], dz = b[2]-p[2]; db = dx*dx + dy*dy + dz*dz; }
        return (db < da) ? g_nearCtr : t;
    }
}

static unsigned long long target_chr(unsigned long long arg1)
{
    unsigned long long chrman = 0, tmp = 0, chr = 0;
    if (!rd_ptr(arg1 + 8, &chrman) || chrman == 0) return 0;
    if (!rd_ptr(chrman + 0x1FF8, &tmp) || tmp == 0) return 0;
    if (!rd_ptr(tmp + 0x18, &chr) || chr == 0) return 0;
    return chr;
}

// ??????? [ChrIns+8] -> ????, ??? +0xE0
static bool chr_pos(unsigned long long chr, float* p)
{
    unsigned long long t = 0;
    if (!rd_ptr(chr + 8, &t) || t == 0) return false;
    return safe_read(t + 0xE0, p, 12);
}

// ---------- ???? ----------
struct Rec {
    unsigned int t;
    unsigned long long arg1, arg2, arg3;
    unsigned long long chr, player;
    int isPlayer;
    int flags;
    int hp0, hp1;
    int dmg, f28, f1ca, f2c;
    int pdmg, f208, f20c;
    int gnodmg;
    float v[5];
    int hasDump;
    unsigned char dump[DUMP_BYTES];
    int hasAtk;
    unsigned char atk[ATK_BYTES];
};

#define NBUF 4096
static Rec           g_buf[NBUF];
static volatile LONG g_head = 0;
static volatile LONG g_total = 0;
// ---- ?????? ----
static volatile LONG g_playerHitMs = 0;      // ??????"???"???/??????
// ??????"????"(???/0 ??) -> ?? fastkey ?????
static volatile LONG g_hitPending = 0;
static volatile unsigned long long g_hitAtkMan = 0;
// "??????"? 0xB68D00 ? arg1 ?? ?? ? hook_c ????
static volatile unsigned long long g_playerA1 = 0;
// 0xB68D00 ?????????(????????????, ?????? hook_c ?)
static volatile unsigned long long g_resA1 = 0;
// ??"???????"??? ?? hook_c ???
static volatile LONG g_hitSeq = 0;
// ?????????: ?????????(????????????????????)
static volatile DWORD g_hitSuppressUntil = 0;
// ????(????)????"???????"??? ?? ?????????????????
static volatile LONG g_anyHitMs = 0;
static volatile float g_reachMargin = 2.0f;   // ★v109: 射程余量(米)
static volatile float g_reachCap    = 4.5f;   // ★v109: 单招射程上限(米)
static volatile float g_rngTol      = 3.0f;   // ★v109: 学到命中状态的距离容差(米)
static volatile DWORD g_lastPlayerResMs = 0;  // ★v110: 最近一次"攻击结算到我头上"的时刻

// ★★ v112: "起手认不出招"的兜底(回溯判定)。
//   日志证明普通挥刀不触发的原因是: 按闪避那一刻敌人还在**起手动画**里(atk=0 占 53%),
//   我们根本认不出"这是攻击"。所以按下闪避时先记下"我这一下要躲的是谁",
//   之后 0.7 秒里盯着它: 只要它进入**认得出来的判定窗口**、距离也在射程里、
//   而且这段时间我没受伤 -> 就算成功闪避, 起缓速(严格性不变: 这刀本来会砍中我)。
static volatile unsigned long long g_armCtr = 0;   // 记下来的敌人容器
static volatile DWORD g_armAt = 0;                 // 按下闪避的时刻
static volatile LONG  g_armHitSnap = 0;            // 按下时的受击计数
static volatile LONG  g_armN = 0;                  // 靠这条路触发了几次(日志用)
static volatile int   g_armSawDodge = 0;           // ★v114: 这期间玩家的"垫步"真的出来了
// ★★ v118: 取消链 —— "技能被垫步取消"出来的成功闪避, 给强化版缓速
static volatile DWORD g_cancelDodgeAt = 0;         // 最近一次"技能 -> 垫步"取消的时刻
static volatile float g_slowSpeedNow = 0.25f;      // 这一次缓速用的倍速
static volatile DWORD g_slowDurNow = 1000;         // 这一次缓速的时长(ms)
static volatile LONG  g_cancelChainN = 0;          // 强化缓速触发了几次(日志用)
static volatile DWORD g_skillCutUntil = 0;         // ★v120: 这段里持续把技能动画推到底
static volatile int   g_skillCutAnim = -1;
static volatile DWORD g_stingPlayedAt = 0;         // ★v129: 上次放提示音的时刻(避免重复/判断是否已提前放)

// ★★ v124: 音效 —— 走"扫描 + 只读验证"拿 FMOD 系统指针, **完全不改游戏代码/不打补丁**
//   (前两次崩的都是给 FMOD::System::update 打代码补丁那条路)。
//   1) 遍历本进程可写内存, 找"对象首字段指向 fmodex64.dll"的候选(那就是 FMOD 对象);
//   2) 用只读 API FMOD_System_GetSoftwareFormat 验证: 返回 0 且采样率合理才算数;
//   3) 拿到之后, 窗口开始/结束各调一次 ChannelGroup::OverrideFrequency(探针已证明有效)。
static volatile unsigned long long g_fmodScanSys = 0;
static volatile LONG g_fmodScanTried = 0;
static volatile LONG g_fmodSets = 0;      // ★v124: 改过几次音频(日志用, 前面那份声明在后面, 这里提前)

static unsigned long long fmod_scan_system(void)
{
    typedef int (WINAPI *SwFmt_t)(void*, int*, void*, int*);
    HMODULE hx = GetModuleHandleA("fmodex64.dll");
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;
    unsigned long long base, end, a;
    SwFmt_t sw;
    MEMORY_BASIC_INFORMATION mbi;
    if (hx == NULL) hx = LoadLibraryA("fmodex64.dll");
    if (hx == NULL) return 0;
    sw = (SwFmt_t)GetProcAddress(hx, "FMOD_System_GetSoftwareFormat");
    if (sw == NULL) return 0;
    dos = (IMAGE_DOS_HEADER*)hx;
    nt = (IMAGE_NT_HEADERS*)((unsigned char*)hx + dos->e_lfanew);
    base = (unsigned long long)(uintptr_t)hx;
    end = base + nt->OptionalHeader.SizeOfImage;
    a = 0;
    while (VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        unsigned long long rstart = (unsigned long long)(uintptr_t)mbi.BaseAddress;
        unsigned long long rsize = (unsigned long long)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) &&
            rsize >= 16 && rsize < 0x10000000ULL)
        {
            unsigned long long* p = (unsigned long long*)rstart;
            unsigned long long n = rsize / 8, i;
            for (i = 0; i < n; i++)
            {
                unsigned long long cand = p[i];
                unsigned long long vt = 0;
                int rate = 0;
                if (cand < 0x10000ULL || (cand & 7) != 0) continue;
                if (!safe_read(cand, &vt, 8)) continue;              // 候选对象首字段 = vtable
                if (vt < base || vt >= end) continue;                // 必须指向 fmodex 内部
                if (sw((void*)(uintptr_t)cand, &rate, NULL, NULL) != 0) continue;  // 只读验证
                if (rate < 8000 || rate > 384000) continue;
                return cand;                                          // 就是它
            }
        }
        a = rstart + rsize;
        if (a == 0 || a > 0x7FFFFFFFFFFFULL) break;
    }
    return 0;
}

static void fmod_override_now(float ratio)
{
    typedef int (WINAPI *GetMaster_t)(void*, void**);
    typedef int (WINAPI *Ovr_t)(void*, float);
    typedef int (WINAPI *SwFmt_t)(void*, int*, void*, int*);
    static GetMaster_t gm = NULL;
    static Ovr_t ov = NULL;
    static SwFmt_t sw = NULL;
    void* mg = NULL;
    int rate = 48000;
    if (g_fmodScanSys == 0) return;
    if (gm == NULL || ov == NULL)
    {
        HMODULE hx = GetModuleHandleA("fmodex64.dll");
        if (hx == NULL) hx = LoadLibraryA("fmodex64.dll");
        if (hx == NULL) return;
        gm = (GetMaster_t)GetProcAddress(hx, "FMOD_System_GetMasterChannelGroup");
        ov = (Ovr_t)GetProcAddress(hx, "FMOD_ChannelGroup_OverrideFrequency");
        sw = (SwFmt_t)GetProcAddress(hx, "FMOD_System_GetSoftwareFormat");
        if (gm == NULL || ov == NULL) return;
    }
    if (sw && sw((void*)(uintptr_t)g_fmodScanSys, &rate, NULL, NULL) != 0) rate = 48000;
    if (gm((void*)(uintptr_t)g_fmodScanSys, &mg) == 0 && mg != NULL)
    {
        ov(mg, (float)rate * (ratio >= 0.995f ? 1.0f : ratio));
        InterlockedIncrement(&g_fmodSets);
    }
}

// ★★ v116: "玩家受击动画"学习表。
//   义父的踩没有伤害、也不是档案里的投技, 但**玩家会进受击/被掀飞的动画**。
//   所以: 每次真的挨刀(掉血)之后 250ms 内, 把玩家当时/之后出现的动画 id 记下来
//   (= 受击动画); 缓速期间只要玩家进了这张表里的动画, 缓速立刻作废。
static int   g_hurtAnim[16];
static int ifr_find(int anim, unsigned short* s, unsigned short* e);   // 前向声明(定义在后面)
static volatile LONG g_hurtN = 0;
static volatile DWORD g_hurtLearnUntil = 0;
static void hurt_learn(int anim)
{
    int i;
    if (anim < 0) return;
    // ★★ v129: 关键修复 —— 有**无敌帧**的动画(垫步、识破/突刺看破这类)不能算"受击动画"。
    //   否则一旦你在识破/垫步时被蹭到一下, 这个动画就被学进"受击表", 之后每次识破都会
    //   被 HURT-CANCEL 当场取消 —— 这就是"识破的缓速怎么没了"的元凶。
    {
        unsigned short s0 = 0, e0 = 0;
        if (ifr_find(anim, &s0, &e0)) return;
    }
    for (i = 0; i < g_hurtN && i < 16; i++) if (g_hurtAnim[i] == anim) return;
    if (g_hurtN < 16) g_hurtAnim[InterlockedIncrement(&g_hurtN) - 1] = anim;
}
static int hurt_is(int anim)
{
    int i;
    if (anim < 0) return 0;
    for (i = 0; i < g_hurtN && i < 16; i++) if (g_hurtAnim[i] == anim) return 1;
    return 0;
}
static volatile LONG g_triggerReq  = 0;
static int   g_atkVals[MAX_CHR][8];        // ??????????"?????"
static int   g_nAtk[MAX_CHR];
static volatile LONG g_triggerCount = 0;
// ---- ?????(????) ----
#define SLOW_SPEED 0.25        // ??????: 0.25 = ?????? (????)
#define SLOW_MS    1000        // ????(????): 1 ? (????)
static DWORD g_slowDur   = SLOW_MS;
static DWORD g_slowStart = 0;
static int   g_slowActive = 0;
static int   g_pActAtStart = -999;
static int   g_atkOpen[MAX_CHR];
static DWORD g_atkStart[MAX_CHR];
// ---- ???? ----
static volatile LONG g_keyLogs = 0;
static volatile LONG g_lastBtn  = -1;        // XInput ?????
static int is_move_key(int vk)
{
    return (vk == 0x57 || vk == 0x41 || vk == 0x53 || vk == 0x44 ||
            vk == 0x26 || vk == 0x28 || vk == 0x25 || vk == 0x27 ||
            vk == 0x10 || vk == 0xA0 || vk == 0xA1);
}
#define DODGE_VK 160
#define DIST_MAX 2.5f
static volatile LONG g_lastKeys = 0;

// ========== ★v94: "突破限制" (缓速窗口里让玩家能出招) ==========
// 地址: [[[WorldChrMan+0x88]+0x1FF8]+0x28]+0xD00 = 玩家自己的动画 PlaySpeed
//       (chrwatch 的 SUM 行把它打印成 pspeed=, 正常时应该是 1.00)
// MODE 里写 nolk 可以整体关掉(只用来做 A/B 对比)
#define UNLOCK_PS   4.0f       // = 1 / SLOW_SPEED
static volatile LONG  g_unlockOn     = 1;
static volatile float g_unlockPS     = UNLOCK_PS;   // 实际用的倍速(MODE 里写 half 就是 2.0)
static volatile LONG  g_unlockWrites = 0;   // 累计写入次数
static volatile LONG  g_unlockKeeps  = 0;   // 窗口内"按了键也不关"的次数
static volatile LONG  g_unlockWinN   = 0;   // 走过的窗口数
static volatile float g_psLast       = -1.0f;

extern "C" void hook_c(void)
{
    Rec r;
    memset(&r, 0, sizeof(r));
    r.t = GetTickCount();
    r.arg1 = g_arg1;
    r.arg2 = g_arg2;
    r.arg3 = g_arg3;
    r.player = g_player;

    unsigned long long chr = target_chr(r.arg1);
    r.chr = chr;
    r.isPlayer = (chr != 0 && chr == r.player) ? 1 : 0;

    if (chr != 0)
    {
        unsigned char f = 0;
        rd_u8(chr + 0x228, &f);
        r.flags = f;
        rd_u32(chr + 0x130, &r.hp0);
        r.hp1 = r.hp0;
    }
    if (r.arg3 != 0)
    {
        unsigned long long a = r.arg3;
        rd_u32(a + 0x1E0, &r.dmg);
        rd_u32(a + 0x1E4, &r.pdmg);
        rd_u32(a + 0x208, &r.f208);
        rd_u32(a + 0x20C, &r.f20c);
        rd_u32(a + 0x28,  &r.f28);
        rd_u32(a + 0x1CA, &r.f1ca);
        rd_u32(a + 0x2C,  &r.f2c);
        safe_read(a, r.v, sizeof(r.v));
    }
    // ????? IsInvincible() ??????????0x143D7A372?
    {
        unsigned char g = 0;
        if (g_base && rd_u8(g_base + 0x3D7A372ULL, &g)) r.gnodmg = g;
    }
    // ???????? ChrIns ? 0x300 ??????????/????????????
    if (chr != 0 && chr == r.player)
    {
        if (safe_read(chr, r.dump, DUMP_BYTES)) r.hasDump = 1;
    }
    // register BOTH sides of the hit: victim (a1->chr) and attacker (a2->chr)
    {
        unsigned long long pair[2];
        int cnt = 0;
        if (chr != 0) pair[cnt++] = chr;
        if (r.arg2 != 0)
        {
            unsigned long long atk = target_chr(r.arg2);
            if (atk != 0 && (cnt == 0 || atk != pair[0])) pair[cnt++] = atk;
        }
        for (int i = 0; i < cnt; i++)
        {
            LONG n = InterlockedIncrement(&g_npend) - 1;
            if (n < 8) g_pend[n & 7] = pair[i];
        }
    }
    // ????????? 0xB6C880 ???"????????"????????
    if (r.arg3 != 0)
    {
        if (safe_read(r.arg3, r.atk, ATK_BYTES)) r.hasAtk = 1;
    }

    InterlockedIncrement(&g_total);
    if (r.isPlayer) g_playerHitMs = (LONG)GetTickCount();
    g_anyHitMs = (LONG)GetTickCount();
    InterlockedIncrement(&g_hitSeq);
    if (r.isPlayer)
    {
        // a2 = ??? Man??? fastkey ??? dump ??(?????????)
        g_hitAtkMan = r.arg2;
        InterlockedExchange(&g_hitPending, 1);
        // ? ??"????????, ????? arg1 ????"?
        //   ??? r.arg1 ?? ??????????
        //   (v57 ???? g_resA1, ??**?????**(???? 0xB68D00) ? arg1,
        //    ???????, ???????)
        if (r.arg1 != 0) g_playerA1 = r.arg1;
    }
    // ??????? a1 / ??? a2?????????????????????????
    {
        unsigned long long mans[2];
        int cnt = 0, i2;
        mans[cnt++] = r.arg1;
        if (r.arg2 != 0) mans[cnt++] = r.arg2;
        for (i2 = 0; i2 < cnt; i2++)
        {
            unsigned long long ctr = ctr_of_man(mans[i2]);
            unsigned long long ch2 = 0;
            LONG n2;
            if (ctr == 0) continue;
            if (!rd_ptr(ctr + 0x18, &ch2) || ch2 == 0) continue;
            n2 = InterlockedIncrement(&g_npend) - 1;
            if (n2 < 8) { g_pend[n2 & 7] = ch2; g_pendCtr[n2 & 7] = ctr; }
        }
    }
    LONG h = InterlockedIncrement(&g_head) - 1;
    g_buf[h % NBUF] = r;
}

// ????????"???? HP"???hook_c ????????
static FILE* g_log = NULL;
static volatile LONG g_install_ok = -1;
static volatile LONG g_ts_ok = -1;
static int   g_ready = 0;
static DWORD g_readyAt = 0;
static volatile LONG g_fcCalls = 0;      // find_container ?????(?????)
static volatile LONG g_lastHitForDiag = 0;      // frame-timescale hook state      // -1 ??? / 0 ?? / 1 ??
static unsigned char g_probe[PROLOG_LEN];
static volatile LONG g_probe_ok = 0;
static char g_mode[32] = "none";

// ????????? [P+0x18] == ChrIns
static unsigned long long find_container(unsigned long long chr)
{
    unsigned int off;
    InterlockedIncrement(&g_fcCalls);
    // CT ???????: ChrIns = Man+0x1F40, container = Man+0x1FF8
    //   => container = ChrIns + 0xB8
    // ????? 0..0x400, ????????? +0xB8 ?????????,
    // ?? g_ctrOf[] ??? 0(DIST ??? C-1)?
    {
        unsigned long long p = 0, v = 0;
        if (rd_ptr(chr + 0xB8, &p) && (p & 7) == 0 && p != 0 &&
            rd_ptr(p + 0x18, &v) && v == chr)
            return p;
    }
    for (off = 0; off < 0x400; off += 8)
    {
        unsigned long long p = 0, v = 0;
        if (!rd_ptr(chr + off, &p) || p == 0) continue;
        if (rd_ptr(p + 0x18, &v) && v == chr) return p;
    }
    return 0;
}

static bool looks_like_chr(unsigned long long c, bool needVt)
{
    if (c < 0x10000ULL || c > 0x7FFFFFFFFFFFULL) return false;
    unsigned long long vt = 0;
    if (!rd_ptr(c, &vt) || vt < 0x10000ULL || vt > 0x7FFFFFFFFFFFULL) return false;
    if (needVt && g_chrVt != 0 && vt != g_chrVt) return false;   // ????????
    unsigned int vtp = 0;
    if (!safe_read(vt, &vtp, 4)) return false;           // ????
    int hp = -1;
    if (!rd_u32(c + 0x130, &hp)) return false;
    if (hp <= 0 || hp > 200000) return false;            // ??? HP ?? > 0
    unsigned int act = 0;
    if (!safe_read(c + 0x230, &act, 4)) return false;
    if (act > 1000000) return false;
    // ???????????????????????????
    unsigned long long t = 0;
    float p[3];
    if (!rd_ptr(c + 8, &t) || t == 0) return false;
    if (!safe_read(t + 0xE0, p, 12)) return false;
    for (int i = 0; i < 3; i++)
        if (!(p[i] > -5000.0f && p[i] < 5000.0f)) return false;
    if (p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f) return false;
    return true;
}

static void add_chr(unsigned long long c, bool needVt)
{
    if (c == 0 || !looks_like_chr(c, needVt)) return;
    if (g_chrVt == 0)
    {
        unsigned long long vt = 0;
        if (rd_ptr(c, &vt)) g_chrVt = vt;
    }
    LONG n = g_nchr;
    if (n > MAX_CHR) n = MAX_CHR;
    for (LONG i = 0; i < n; i++) if (g_chr[i].addr == c) return;
    if (n >= MAX_CHR) return;
    g_chr[n].addr = c;
    g_chr[n].hp = -1;
    g_chr[n].act = -1;
    g_chr[n].x = g_chr[n].y = g_chr[n].z = 0;
    g_nchr = n + 1;
}

// ? ChrMan ???????
static void discover_chrs(void)
{
    unsigned long long wcm = 0, man = 0, e0 = 0;
    if (!rd_ptr(g_base + 0x3D7A1E0ULL, &wcm) || wcm == 0) return;
    if (!rd_ptr(wcm + 0x88, &man) || man == 0) return;
    if (!rd_ptr(man + 0x1FF8, &e0) || e0 == 0) return;

    // ?? A?[[man+0x1FF8+k*8]+0x18]???????????k=0?
    for (unsigned long long off = 0; off < 0x200; off += 8)
    {
        unsigned long long p = 0, c = 0;
        if (!rd_ptr(man + 0x1FF8 + off, &p) || p == 0) continue;
        if (rd_ptr(p + 0x18, &c)) add_chr(c, true);
        add_chr(p, true);
    }
    // ?? B?[e0+k*8] ???? ChrIns
    for (unsigned long long off = 0; off < 0x200; off += 8)
    {
        unsigned long long p = 0, c = 0;
        if (!rd_ptr(e0 + off, &p) || p == 0) continue;
        if (rd_ptr(p + 0x18, &c)) add_chr(c, true);
        add_chr(p, true);
    }
}

static void read_mode(char* dst, size_t n)
{
    FILE* mf = NULL;
    strcpy_s(dst, n, "none");
    if (fopen_s(&mf, MODEPATH, "r") == 0 && mf != NULL)
    {
        if (fgets(dst, (int)n, mf) == NULL) strcpy_s(dst, n, "none");
        for (char* q = dst; *q; q++) { if (*q == '\r' || *q == '\n') { *q = 0; break; } }
        fclose(mf);
    }
}

// ---------- ??????????? + ????? mode ??? ----------
#define TL_SELF   0x600
#define TL_MAXSUB 64
#define TL_SUBLEN 0x400

static DWORD WINAPI timeline(LPVOID)
{
    FILE* f = NULL;
    fopen_s(&f, TIMEPATH, "w");
    if (f == NULL) return 0;
    fputs("# timeline start (player ChrIns change-log)\n", f);
    fflush(f);

    static unsigned char prevSelf[TL_SELF], curSelf[TL_SELF];
    static unsigned char prevSub[TL_MAXSUB][TL_SUBLEN];
    static unsigned char curSub[TL_MAXSUB][TL_SUBLEN];
    unsigned long long subAddr[TL_MAXSUB];
    unsigned long long subAddrPrev[TL_MAXSUB];
    int  nsubPrev = -1;
    int  nsub = 0, subFor = 0;
    unsigned int selfLen = 0;
    bool primed = false;
    DWORD refresh = 0, lastLog = 0;
    DWORD refreshSub = 0, lastBeat = 0, lastFull = 0;
    DWORD lastSubFull = 0;
    long  written = 0;
    DWORD endAt = GetTickCount() + 1800000;

    while (GetTickCount() < endAt)
    {
        DWORD now = GetTickCount();
        if (now - refresh > 1000)
        {
            refresh = now;
            read_mode(g_mode, sizeof(g_mode));
            unsigned long long chr = resolve_player();
            if (chr != 0)
            {
                g_player = chr;
                if (subFor != (int)chr || (now - refreshSub) > 5000)
                {
                    refreshSub = now;
                    subFor = (int)chr;
                    nsub = 0;
                    for (unsigned int off = 0; off < 0x400 && nsub < TL_MAXSUB; off += 8)
                    {
                        unsigned long long cand = 0;
                        if (!rd_ptr(chr + off, &cand) || cand == 0) continue;
                        unsigned int probe = 0;
                        if (!safe_read(cand, &probe, 4)) continue;
                        subAddr[nsub++] = cand;
                    }
                    // ??????????????????????????
                    bool same = (nsub == nsubPrev);
                    if (same)
                        for (int i = 0; i < nsub; i++)
                            if (subAddrPrev[i] != subAddr[i]) { same = false; break; }
                    if (!same)
                    {
                        fprintf(f, "# subs:");
                        for (int i = 0; i < nsub; i++) fprintf(f, " %d=%llX", i, subAddr[i]);
                        fputs("\n", f);
                        for (int i = 0; i < nsub; i++)
                        {
                            unsigned char tmp[TL_SUBLEN];
                            if (!safe_read(subAddr[i], tmp, TL_SUBLEN)) continue;
                            fprintf(f, "# SUBINIT %d %llX ", i, subAddr[i]);
                            for (int j = 0; j < TL_SUBLEN; j++) fprintf(f, "%02X", tmp[j]);
                            fputs("\n", f);
                        }
                        memcpy(subAddrPrev, subAddr, sizeof(subAddr));
                        nsubPrev = nsub;
                        fflush(f);
                    }
                }
            }
        }

        unsigned long long chr = g_player;
        if (chr != 0 && selfLen == 0)
        {
            // ???????????????
            if (safe_read(chr, curSelf, TL_SELF)) selfLen = TL_SELF;
            else if (safe_read(chr, curSelf, 0x400)) selfLen = 0x400;
            else if (safe_read(chr, curSelf, 0x300)) selfLen = 0x300;
        }
        if (chr != 0 && selfLen != 0 && safe_read(chr, curSelf, selfLen))
        {
            int act = *(int*)(curSelf + 0x230);
            for (int i = 0; i < nsub; i++) safe_read(subAddr[i], curSub[i], TL_SUBLEN);

            if (!primed)
            {
                memcpy(prevSelf, curSelf, selfLen);
                for (int i = 0; i < nsub; i++) memcpy(prevSub[i], curSub[i], TL_SUBLEN);
                primed = true;
            }
            else
            {
                if ((now - lastLog) >= 16 && written < 60000)
                {
                    char line[16384];
                    int  pos = 0, nch = 0;
                    pos += _snprintf_s(line + pos, sizeof(line) - pos, _TRUNCATE,
                                       "t=%u mode=%s act=%d chg=", now, g_mode, act);
                    for (unsigned int off = 0; off + 4 <= selfLen && pos < 2200; off += 4)
                    {
                        unsigned int a = *(unsigned int*)(prevSelf + off);
                        unsigned int b = *(unsigned int*)(curSelf + off);
                        if (a != b)
                        {
                            pos += _snprintf_s(line + pos, sizeof(line) - pos, _TRUNCATE,
                                               "%X:%u ", off, b);
                            nch++;
                        }
                    }
                    // ????????????????????????????
                    for (int si = 0; si < nsub && pos < 7000; si++)
                    {
                        int s = (si + (int)(written % (nsub ? nsub : 1))) % (nsub ? nsub : 1);
                        for (int off = 0; off + 4 <= TL_SUBLEN && pos < 7000; off += 4)
                        {
                            unsigned int a = *(unsigned int*)(prevSub[s] + off);
                            unsigned int b = *(unsigned int*)(curSub[s] + off);
                            if (a != b)
                            {
                                pos += _snprintf_s(line + pos, sizeof(line) - pos, _TRUNCATE,
                                                   "S%d+%X:%u ", s, off, b);
                                nch++;
                            }
                        }
                    }
                    if (nch > 0)
                    {
                        fputs(line, f);
                        fputs("\n", f);
                        lastLog = now;
                        written++;
                        if ((written & 63) == 0) fflush(f);
                    }
                }
                // ???? 2 ??????????????????????
                if ((now - lastBeat) >= 2000)
                {
                    lastBeat = now;
                    fprintf(f, "t=%u HB mode=%s act=%d hp=%d f228=%02X f22C=%u f174=%d pos=%.2f/%.2f/%.2f\n",
                            (unsigned)now, g_mode, act,
                            *(int*)(curSelf + 0x130),
                            curSelf[0x228],
                            *(unsigned int*)(curSelf + 0x22C),
                            *(int*)(curSelf + 0x174),
                            *(float*)(curSelf + 0x15C), *(float*)(curSelf + 0x160),
                            *(float*)(curSelf + 0x164));
                    fflush(f);
                }
                // ? 10 ?????????????????
                if ((now - lastFull) >= 10000)
                {
                    lastFull = now;
                    fprintf(f, "t=%u FULL mode=%s ", (unsigned)now, g_mode);
                    for (unsigned int i = 0; i < selfLen; i++) fprintf(f, "%02X", curSelf[i]);
                    fputs("\n", f);
                    fflush(f);
                }
                // ? 30 ???????????????????????????
                if ((now - lastSubFull) >= 30000)
                {
                    lastSubFull = now;
                    for (int s = 0; s < nsub; s++)
                    {
                        fprintf(f, "# SUBFULL %d %llX ", s, subAddr[s]);
                        for (int j = 0; j < TL_SUBLEN; j++) fprintf(f, "%02X", curSub[s][j]);
                        fputs("\n", f);
                    }
                    fflush(f);
                }
                memcpy(prevSelf, curSelf, selfLen);
                for (int i = 0; i < nsub; i++) memcpy(prevSub[i], curSub[i], TL_SUBLEN);
            }
        }
        Sleep(2);
    }
    fputs("# timeline stop\n", f);
    fclose(f);
    return 0;
}


// ---------- ????????? / ?? / ???? ----------
// ---------- ?????CE ???????????????? ----------
static volatile double   g_clockSpeed = 1.0;
static volatile LONGLONG g_realBase = 0, g_virtBase = 0;

typedef BOOL (WINAPI *QPC_t)(LARGE_INTEGER*);
typedef ULONGLONG (WINAPI *GTC64_t)(void);
typedef DWORD (WINAPI *GTC_t)(void);
typedef DWORD (WINAPI *TGT_t)(void);

static QPC_t   g_realQPC = NULL;
static GTC64_t g_realGTC64 = NULL;
static GTC_t   g_realGTC = NULL;
static TGT_t   g_realTGT = NULL;

static volatile LONG g_clkCalls = 0;

// ================= ?????"??????" =================
// ????(Zullie the Witch / Me_TheCat), ? SekiroFpsUnlockAndMore ??:
//   pTimeRelated -> [TimescaleManager+0x360] = <float>fTimescale
//   "acts as a global speed scale for almost all ingame calculations"
//   0x140000000 + 0x3C8D308 ??? TimescaleManager ??
//   AOB(????): 48 8B 05 ?? ?? ?? ?? F3 0F 10 88 ?? ?? ?? ??
// ?: ????????? [[[[+0x88]+0x1FF8]+0x28]+0xD00] ?? ????????? PlaySpeed,
//     ??????? fTimescalePlayer?
// ? ????? IAT ? QueryPerformanceCounter ?"??", ????????? ?? ???
//   "????????????"???????????????????
static volatile unsigned long long g_tsAddr = 0;     // fTimescale ???
static volatile double g_gameSpeed = 1.0;
static void set_clock_speed(double s);   // ?????, ?"????"????(????)

static bool resolve_timescale(void)
{
    unsigned long long tm = 0;
    float f = 0.0f;
    unsigned char b[2];
    // ?? AOB ??????? 0x114A7C7 ?? mov rax,[rip+...]
    if (safe_read(g_base + 0x114A7C7ULL, b, 2) && b[0] == 0x48 && b[1] == 0x8B)
    {
        if (rd_ptr(g_base + 0x3C8D308ULL, &tm) && tm != 0 &&
            safe_read(tm + 0x360, &f, 4) && f > 0.001f && f < 100.0f)
        {
            g_tsAddr = tm + 0x360;
            return true;
        }
    }
    return false;
}

static void set_game_speed(double s)
{
    if (g_tsAddr == 0) { set_clock_speed(s); return; }   // ??: ??????
    if (s == g_gameSpeed) return;
    g_gameSpeed = s;
    {
        float f = (float)s;
        safe_write(g_tsAddr, &f, 4);
    }
}
static BOOL WINAPI hkQPC(LARGE_INTEGER* out)
{
    LARGE_INTEGER r;
    double s;
    InterlockedIncrement(&g_clkCalls);
    if (g_realQPC == NULL) return FALSE;
    if (!g_realQPC(&r)) return FALSE;
    s = g_clockSpeed;
    if (s == 1.0) { out->QuadPart = r.QuadPart; return TRUE; }
    out->QuadPart = (LONGLONG)((double)g_virtBase +
                     (double)((LONGLONG)r.QuadPart - g_realBase) * s);
    return TRUE;
}

static volatile LONGLONG g_msRealBase = 0, g_msVirtBase = 0;
static volatile double   g_msSpeed = 1.0;

static LONGLONG scale_ms(LONGLONG real)
{
    double s = g_clockSpeed;
    if (s == 1.0 && g_msSpeed == 1.0) return real;
    if (s != g_msSpeed)
    {
        double cur = (double)g_msVirtBase + (double)(real - g_msRealBase) * g_msSpeed;
        g_msRealBase = real; g_msVirtBase = (LONGLONG)cur; g_msSpeed = s;
    }
    if (s == 1.0) return real;
    return (LONGLONG)((double)g_msVirtBase + (double)(real - g_msRealBase) * s);
}

static ULONGLONG WINAPI hkGTC64(void)
{
    if (g_realGTC64 == NULL) return 0;
    return (ULONGLONG)scale_ms((LONGLONG)g_realGTC64());
}
static DWORD WINAPI hkGTC(void)
{
    if (g_realGTC == NULL) return 0;
    return (DWORD)scale_ms((LONGLONG)(DWORD)g_realGTC());
}
static DWORD WINAPI hkTGT(void)
{
    if (g_realTGT == NULL) return 0;
    return (DWORD)scale_ms((LONGLONG)(DWORD)g_realTGT());
}

// ???????????"??????"???????????
static void set_clock_speed(double s)
{
    if (s == g_clockSpeed) return;
    if (g_realQPC != NULL)
    {
        LARGE_INTEGER r;
        if (g_realQPC(&r))
        {
            double cur = (double)g_virtBase +
                         (double)((LONGLONG)r.QuadPart - g_realBase) * g_clockSpeed;
            g_realBase = r.QuadPart;
            g_virtBase = (LONGLONG)cur;
        }
    }
    g_clockSpeed = s;
}

static int g_clockHooks = 0;

static void* hook_iat(HMODULE mod, const char* fnName, void* newFn, void** origOut)
{
    unsigned char* b = (unsigned char*)mod;
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;
    IMAGE_DATA_DIRECTORY* imp;
    IMAGE_IMPORT_DESCRIPTOR* desc;
    if (mod == NULL) return NULL;
    dos = (IMAGE_DOS_HEADER*)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    imp = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp->VirtualAddress == 0) return NULL;
    for (desc = (IMAGE_IMPORT_DESCRIPTOR*)(b + imp->VirtualAddress); desc->Name != 0; desc++)
    {
        IMAGE_THUNK_DATA* oft = (IMAGE_THUNK_DATA*)(b + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(b + desc->FirstThunk);
        if (desc->OriginalFirstThunk == 0) continue;
        for (; oft->u1.AddressOfData != 0; oft++, ft++)
        {
            IMAGE_IMPORT_BY_NAME* nm;
            DWORD oldp = 0;
            void* old;
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;
            nm = (IMAGE_IMPORT_BY_NAME*)(b + oft->u1.AddressOfData);
            if (strcmp((const char*)nm->Name, fnName) != 0) continue;
            old = (void*)(uintptr_t)ft->u1.Function;
            if (!VirtualProtect(&ft->u1.Function, sizeof(void*), PAGE_READWRITE, &oldp)) return NULL;
            ft->u1.Function = (ULONGLONG)(uintptr_t)newFn;
            VirtualProtect(&ft->u1.Function, sizeof(void*), oldp, &oldp);
            if (origOut) *origOut = old;
            g_clockHooks++;
            return old;
        }
    }
    return NULL;
}

static void install_clock_hooks(void)
{
    HMODULE game = (HMODULE)(uintptr_t)g_base;
    hook_iat(game, "QueryPerformanceCounter", (void*)&hkQPC, (void**)&g_realQPC);
    hook_iat(game, "GetTickCount64", (void*)&hkGTC64, (void**)&g_realGTC64);
    hook_iat(game, "GetTickCount", (void*)&hkGTC, (void**)&g_realGTC);
    hook_iat(game, "timeGetTime", (void*)&hkTGT, (void**)&g_realTGT);
}

// ---------- ????????"???"????----------
typedef DWORD (WINAPI *XIGS_t)(DWORD, void*);
// ---------- ??"????"? Man?AOB ???????----------
#define F_TARGETMAN 0x009C3ADAULL
#define TM_LEN      20
static void suspend_others(bool);
static volatile LONG g_tm_ok = -1;
static unsigned char g_tmProbe[20];
static volatile LONG g_tmProbeOk = 0;
static bool install_targetman(void)
{
    unsigned long long tgt = g_base + F_TARGETMAN;
    static const unsigned char expect[TM_LEN] = {
        0x48,0x8B,0x80,0xF8,0x1F,0x00,0x00,
        0x48,0x8B,0x08,
        0x48,0xB8,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00
    };
    unsigned char orig[TM_LEN];
    unsigned char* tr;
    unsigned char patch[14];
    DWORD oldp = 0;
    if (!safe_read(tgt, orig, TM_LEN)) return false;
    memcpy((void*)g_tmProbe, orig, TM_LEN);      // ????????????
    g_tmProbeOk = 1;
    if (memcmp(orig, expect, TM_LEN) != 0) return false;
    g_tmProbeOk = 0;
    tr = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return false;
    memcpy(tr, orig, TM_LEN);
    tr[TM_LEN] = 0xFF; tr[TM_LEN + 1] = 0x25;
    *(unsigned int*)(tr + TM_LEN + 2) = 0;
    *(unsigned long long*)(tr + TM_LEN + 6) = tgt + TM_LEN;
    g_tramp2 = tr;
    patch[0] = 0xFF; patch[1] = 0x25;
    *(unsigned int*)(patch + 2) = 0;
    *(unsigned long long*)(patch + 6) = (unsigned long long)(uintptr_t)&tgt_stub;
    if (!VirtualProtect((LPVOID)(uintptr_t)tgt, 14, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    suspend_others(true);
    memcpy((void*)(uintptr_t)tgt, patch, 14);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)tgt, 14);
    suspend_others(false);
    VirtualProtect((LPVOID)(uintptr_t)tgt, 14, oldp, &oldp);
    return true;
}

typedef SHORT (WINAPI *GASK_t)(int);
typedef BOOL  (WINAPI *GKS_t)(PBYTE);

static XIGS_t g_realXIGS = NULL;
static GASK_t g_realGASK = NULL;
static GKS_t  g_realGKS  = NULL;
static volatile LONG g_keyDown[8];          // 256 ????????
static volatile LONG g_xinputHooks = 0, g_kbdHooks = 0;

static void set_key_bit(int vk, int down)
{
    LONG w, b;
    if (vk < 1 || vk > 255) return;
    w = vk >> 5; b = 1L << (vk & 31);
    if (down) InterlockedOr(&g_keyDown[w], b);
    else      InterlockedAnd(&g_keyDown[w], ~b);
}

static DWORD WINAPI hkXIGS(DWORD idx, void* st)
{
    DWORD r;
    if (g_realXIGS == NULL) return 1167;    // ERROR_DEVICE_NOT_CONNECTED
    r = g_realXIGS(idx, st);
    if (r == 0 && st != NULL)
        g_lastBtn = *(unsigned short*)((unsigned char*)st + 4);   // wButtons
    return r;
}

static SHORT WINAPI hkGASK(int vk)
{
    SHORT r;
    if (g_realGASK == NULL) return 0;
    r = g_realGASK(vk);
    set_key_bit(vk, (r & 0x8000) ? 1 : 0);
    return r;
}

static BOOL WINAPI hkGKS(PBYTE ks)
{
    BOOL r;
    if (g_realGKS == NULL) return FALSE;
    r = g_realGKS(ks);
    if (r && ks != NULL)
        for (int vk = 1; vk < 256; vk++) set_key_bit(vk, (ks[vk] & 0x80) ? 1 : 0);
    return r;
}

static void install_input_hooks(void)
{
    HMODULE game = (HMODULE)(uintptr_t)g_base;
    hook_iat(game, "XInputGetState", (void*)&hkXIGS, (void**)&g_realXIGS);
    hook_iat(game, "GetAsyncKeyState", (void*)&hkGASK, (void**)&g_realGASK);
    hook_iat(game, "GetKeyboardState", (void*)&hkGKS, (void**)&g_realGKS);
    if (g_realXIGS) g_xinputHooks = 1;
    if (g_realGASK || g_realGKS) g_kbdHooks = 1;
}

static bool is_slow_mode(const char* m)
{
    char buf[64];
    int i = 0;
    for (; m[i] != 0 && i < 63; i++) buf[i] = (char)tolower((unsigned char)m[i]);
    buf[i] = 0;
    return strstr(buf, "slow") != NULL;
}

static bool install_ts(void);
static DWORD WINAPI chrwatch(LPVOID)
{
    FILE* f = NULL;
    fopen_s(&f, CHARPATH, "w");
    if (f == NULL) return 0;
    fputs("# char watch start\n", f);
    fflush(f);

    unsigned long long player = 0;
    unsigned long long chrman = 0;
    DWORD refresh = 0, lastP = 0, lastBeat = 0, lastDisc = 0, endAt = GetTickCount() + 1800000;
    int   lhp[MAX_CHR], lact[MAX_CHR];
    float lastPX = 0, lastPY = 0, lastPZ = 0;
    int   haveP = 0;

    for (int i = 0; i < MAX_CHR; i++) { lhp[i] = -12345; lact[i] = -12345; }

    while (GetTickCount() < endAt)
    {
        DWORD now = GetTickCount();
        double bodyT0 = qms();
        if (!g_ready) { if (g_player == 0) g_player = resolve_player(); if (g_player != 0) { if (g_readyAt == 0) g_readyAt = now + 8000; if (now >= g_readyAt) g_ready = 1; } Sleep(200); continue; }

        // ????ID ??????? fastkey ??(?? 16ms ????)
        if (now - refresh > 3000)
        {
            refresh = now;
            read_mode(g_mode, sizeof(g_mode));
            unsigned long long pr = resolve_player();
            {
                unsigned long long wcm = 0;
                if (rd_ptr(g_base + 0x3D7A1E0ULL, &wcm) && wcm)
                    rd_ptr(wcm + 0x88, &chrman);
            }
            if (pr != 0) player = pr;
            add_chr(player, false);
            LONG np = g_npend;
            for (LONG i = 0; i < np && i < 8; i++)
            {
                int n0 = g_nchr, k2;
                unsigned long long c0 = g_pend[i & 7];
                add_chr(c0, false);
                for (k2 = 0; k2 < g_nchr; k2++)
                    if (g_chr[k2].addr == c0) { g_ctrOf[k2] = g_pendCtr[i & 7]; break; }
                (void)n0;
            }
            if (now - lastDisc > 30000) { lastDisc = now; discover_chrs(); }
        }

        unsigned long long pchr = (player != 0) ? player : resolve_player();
        {   // ?????????????????????????????
            unsigned long long rp = resolve_player();
            if (rp != 0) { player = rp; pchr = rp; }
        }
        if (player == 0) player = pchr;

        // ---- ???????????????????"????"????----
        {
            static DWORD lastDist = 0;
            if (now - lastDist >= 1000)
            {
                unsigned long long pctr = ctr_of_player();
                float pp[3] = {0, 0, 0};
                int   nn3 = (g_nchr > MAX_CHR) ? MAX_CHR : g_nchr;
                float best = -1.0f, bx = 0, by = 0, bz = 0;
                int   bi = -1, i;
                lastDist = now;
                if (pctr != 0 && ctr_pos(pctr, pp))
                {
                    for (i = 0; i < nn3; i++)
                    {
                        float ep[3];
                        float dx, dy, dz, d;
                        if (g_chr[i].addr == pchr) continue;
                        if (g_ctrOf[i] == 0) g_ctrOf[i] = find_container(g_chr[i].addr);
                        if (g_ctrOf[i] == 0) continue;
                        if (!ctr_pos(g_ctrOf[i], ep)) continue;
                        dx = ep[0] - pp[0]; dy = ep[1] - pp[1]; dz = ep[2] - pp[2];
                        d = sqrtf(dx * dx + dy * dy + dz * dz);
                        if (best < 0.0f || d < best) { best = d; bi = i; bx = ep[0]; by = ep[1]; bz = ep[2]; }
                    }
                }
                fprintf(f, "t=%u mode=%s DIST C%d d=%.2f  me=%.2f/%.2f/%.2f  foe=%.2f/%.2f/%.2f\n",
                        (unsigned)now, g_mode, bi, best, pp[0], pp[1], pp[2], bx, by, bz);
                // ???????: ??"?????"??
                g_nearCtr = (bi >= 0) ? g_ctrOf[bi] : 0;
                // ?????hook 0x9C3ADA ??? Man????
                if (g_targetMan != 0)
                {
                    unsigned long long ectr = 0;
                    float ep[3];
                    if (rd_ptr(g_targetMan + 0x1FF8, &ectr) && ectr != 0 && ctr_pos(ectr, ep))
                    {
                        float dx = ep[0] - pp[0], dy = ep[1] - pp[1], dz = ep[2] - pp[2];
                        fprintf(f, "t=%u mode=%s TARGET d=%.2f  foe=%.2f/%.2f/%.2f  tm=%llX\n",
                                (unsigned)now, g_mode, sqrtf(dx*dx+dy*dy+dz*dz),
                                ep[0], ep[1], ep[2], g_targetMan);
                    }
                }
            }
        }

        // ---- ???? 100ms ??? (??ID / ????? / ??? / HP) ----
        {
            static DWORD lastES = 0;
            static int   lastEAnim = -999999;
            if (now - lastES >= 500)
            {
                lastES = now;
                {
                    unsigned long long ectr = enemy_ctr_fast();
                    if (ectr != 0)
                    {
                        float eLen = 0.0f;
                        int   eAnim = anim_of_ctr(ectr, &eLen);
                        int   eAct = -1, eHp = -1, pAct = -1;
                        float pd = -1.0f;
                        float ppos2[3] = {0, 0, 0}, epos2[3] = {0, 0, 0};
                        unsigned long long ech = 0, pctr3 = ctr_of_player();
                        if (rd_ptr(ectr + 0x18, &ech) && ech != 0)
                        {
                            rd_u32(ech + 0x130, &eHp);
                            safe_read(ech + 0x230, &eAct, 4);
                        }
                        if (pctr3 != 0 && ctr_pos(pctr3, ppos2) && ctr_pos(ectr, epos2))
                        {
                            float dx = epos2[0] - ppos2[0], dy = epos2[1] - ppos2[1], dz = epos2[2] - ppos2[2];
                            pd = sqrtf(dx * dx + dy * dy + dz * dz);
                        }
                        if (pctr3 != 0)
                        {
                            unsigned long long pch2 = 0;
                            if (rd_ptr(pctr3 + 0x18, &pch2) && pch2 != 0) safe_read(pch2 + 0x230, &pAct, 4);
                        }
                        fprintf(f, "t=%u mode=%s ES dist=%.2f eanim=%d%s elen=%.2f eact=%d ehp=%d pact=%d\n",
                                (unsigned)now, g_mode, pd, eAnim,
                                (eAnim != lastEAnim) ? " *NEW" : "",
                                eLen, eAct, eHp, pAct);
                        lastEAnim = eAnim;
                    }
                }
            }
        }

        // ---- ????? + ????"????ID"?? (400ms ??)
        // ???????????, ???????? ~2Hz ????
        {
            static DWORD lastHeavy = 0;
            static char  lastAnim[128] = "";
            static int   lastEAnimID[MAX_CHR];
            static int   animInit = 0;
            if (now - lastHeavy >= 500)
            {
                lastHeavy = now;
                if (!animInit) { for (int i = 0; i < MAX_CHR; i++) lastEAnimID[i] = -123456; animInit = 1; }
                unsigned long long ctr = ctr_of_player();
                if (ctr != 0)
                {
                    float pp[3] = {0, 0, 0};
                    char  an[128];
                    bool  okp = ctr_pos(ctr, pp);
                    bool  oka = ctr_anim(ctr, an, sizeof(an));
                    if (okp) { lastPX = pp[0]; lastPY = pp[1]; lastPZ = pp[2]; haveP = 1; }
                    if (oka && strcmp(an, lastAnim) != 0)
                    {
                        strncpy_s(lastAnim, sizeof(lastAnim), an, _TRUNCATE);
                        fprintf(f, "t=%u mode=%s ANIM '%.60s' pos=%.2f/%.2f/%.2f\n",
                                (unsigned)now, g_mode, an, pp[0], pp[1], pp[2]);
                        fflush(f);
                    }
                }
                int nn = (g_nchr > MAX_CHR) ? MAX_CHR : g_nchr;
                for (int e = 0; e < nn; e++)
                {
                    unsigned long long ec = g_ctrOf[e];
                    float ep[3] = {0, 0, 0};
                    int   aid = -1;
                    if (ec == 0 || g_chr[e].addr == pchr) continue;
                    aid = anim_of_ctr(ec, NULL);
                    if (aid == lastEAnimID[e]) continue;
                    lastEAnimID[e] = aid;
                    if (ctr_pos(ec, ep))
                    {
                        float dx = ep[0] - lastPX, dy = ep[1] - lastPY, dz = ep[2] - lastPZ;
                        fprintf(f, "t=%u mode=%s EANIMID C%d id=%d dist=%.2f\n",
                                (unsigned)now, g_mode, e, aid, sqrtf(dx * dx + dy * dy + dz * dz));
                    }
                    else
                        fprintf(f, "t=%u mode=%s EANIMID C%d id=%d dist=-1\n",
                                (unsigned)now, g_mode, e, aid);
                }
            }
        }

        // ---- ???????? 16ms ??????? 5mm ?? ----
        if (pchr != 0)
        {
            float px = 0, py = 0, pz = 0;
            int   php = -1, pact = -1;
            float pv[3] = {0, 0, 0};
            bool okpos = chr_pos(pchr, pv);
            if (okpos) { px = pv[0]; py = pv[1]; pz = pv[2]; }
            rd_u32(pchr + 0x130, &php);
            safe_read(pchr + 0x230, &pact, 4);
            if (haveP && okpos && (now - lastP) >= 100)
            {
                float dx = px - lastPX, dy = py - lastPY, dz = pz - lastPZ;
                if (dx * dx + dy * dy + dz * dz > 0.000025f)      // 5mm
                {
                    fprintf(f, "t=%u mode=%s P hp=%d act=%d pos=%.3f/%.3f/%.3f step=%.4f\n",
                            (unsigned)now, g_mode, php, pact, px, py, pz,
                            sqrtf(dx * dx + dy * dy + dz * dz));
                    lastP = now;
                }
            }
            if (okpos) { lastPX = px; lastPY = py; lastPZ = pz; haveP = 1; }
        }

        // ---- ???????HP / ???? ???? ----
        int n = g_nchr;
        if (n > MAX_CHR) n = MAX_CHR;
        for (int i = 0; i < n; i++)
        {
            unsigned long long c = g_chr[i].addr;
            int hp = -1, act = -1;
            float pos[3] = {0, 0, 0};
            float x = 0, y = 0, z = 0;
            if (!rd_u32(c + 0x130, &hp)) continue;
            safe_read(c + 0x230, &act, 4);
            if (chr_pos(c, pos)) { x = pos[0]; y = pos[1]; z = pos[2]; }
            float d = 0;
            if (haveP)
            {
                float dx = x - lastPX, dy = y - lastPY, dz = z - lastPZ;
                d = sqrtf(dx * dx + dy * dy + dz * dz);
            }
            g_chr[i].hp = hp; g_chr[i].act = act;
            g_chr[i].x = x; g_chr[i].y = y; g_chr[i].z = z;

            if (hp != lhp[i] || act != lact[i])
            {
                const char* tag = (c == pchr) ? "PLAYER" : "ENEMY ";
                // ???? 40 ?????????????????
                if (c == pchr || d <= 40.0f)
                    fprintf(f, "t=%u mode=%s %s C%-2d addr=%llX hp=%d->%d act=%d->%d dist=%.2f\n",
                            (unsigned)now, g_mode, tag, i, c, lhp[i], hp, lact[i], act, d);
                lhp[i] = hp; lact[i] = act;
                // (no per-line flush; the 1s heartbeat flushes)
            }
        }

        if (now - lastBeat > 1000)
        {
            lastBeat = now;
            {
                float tsField = 0.0f;
                if (chrman != 0) safe_read(chrman + 0x10D0, &tsField, 4);
            fprintf(f, "t=%u HB mode=%s nchr=%d player=%llX tm_ok=%ld tmMan=%llX\n",
                    (unsigned)now, g_mode, n, pchr, (long)g_tm_ok, g_targetMan);
            if (g_tmProbeOk)
            {
                int z;
                fputs("    TMPROBE ", f);
                for (z = 0; z < 20; z++) fprintf(f, "%02X ", g_tmProbe[z]);
                fputs("\n", f);
            }
            }
            fflush(f);
            fflush(f);
            for (int i = 0; i < n; i++)
                fprintf(f, "    C%-2d %llX hp=%-6d act=%-6d pos=%.2f/%.2f/%.2f\n",
                        i, g_chr[i].addr, g_chr[i].hp, g_chr[i].act,
                        g_chr[i].x, g_chr[i].y, g_chr[i].z);
            fflush(f);
        }

        // ---- ?????? 0xA38236/40/4A ????????????????????????----
        {
            static int constLogged = 0;
            if (!constLogged && g_base != 0)
            {
                constLogged = 1;
                float c1 = 0, c2 = 0, c3 = 0;
                safe_read(g_base + 0x328919CULL, &c1, 4);
                safe_read(g_base + 0x3289168ULL, &c2, 4);
                safe_read(g_base + 0x328917CULL, &c3, 4);
                fprintf(f, "# CONST 0x14328919C=%.6f  0x143289168=%.6f  0x14328917C=%.6f\n",
                        c1, c2, c3);
                fflush(f);
            }
        }

        // ---- ?? [ChrMan+0x10D0]?????????????????? ----
        if (chrman != 0)
        {
            static float lastTS = -1.0f;
            float ts = 0;
            if (safe_read(chrman + 0x10D0, &ts, 4) && ts > 0.0f && ts < 100.0f)
            {
                if (lastTS < 0.0f) lastTS = ts;
                if (ts > lastTS + 0.01f || ts < lastTS - 0.01f)
                {
                    fprintf(f, "t=%u mode=%s TS ????? %.4f -> %.4f\n",
                            (unsigned)now, g_mode, lastTS, ts);
                    fflush(f);
                    lastTS = ts;
                }
            }
        }


        // ---- ?????CE ?????? slow ???????????? 1.0 ----
        // ---- input logging: every key/button change is logged (to find the dodge key) ----
        {
            static LONG lastK[8];
            static LONG lastBtnSeen = -1;
            static DWORD lastKeyLog = 0;
            int changed = 0;
            int i;
            for (i = 0; i < 8; i++) if (g_keyDown[i] != lastK[i]) changed = 1;
            if (g_lastBtn != lastBtnSeen) changed = 1;
            if (changed && (now - lastKeyLog) >= 30)
            {
                int first = 1, vk;
                lastKeyLog = now;
                for (i = 0; i < 8; i++) lastK[i] = g_keyDown[i];
                if (g_lastBtn != lastBtnSeen)
                {
                    lastBtnSeen = g_lastBtn;
                    fprintf(f, "t=%u mode=%s PAD buttons=0x%04X\n",
                            (unsigned)now, g_mode, (unsigned)(lastBtnSeen & 0xFFFF));
                }
                fprintf(f, "t=%u mode=%s KEYS down=", (unsigned)now, g_mode);
                for (vk = 1; vk < 256; vk++)
                    if (g_keyDown[vk >> 5] & (1L << (vk & 31)))
                    {
                        fprintf(f, "%s%d", first ? "" : ",", vk);
                        first = 0;
                    }
                fputs("\n", f);
                fflush(f);
            }
        }

        // ---- enemy attack windows (600ms ??????? act ??????????) ----
        {
            int i;
            for (i = 0; i < n; i++)
            {
                static int lastAv[MAX_CHR];
                int av;
                if (g_chr[i].addr == pchr) continue;
                av = g_chr[i].act;
                if (av != lastAv[i])
                {
                    lastAv[i] = av;
                    if (!(av == 0 || av == 105 || av == 11))
                    {
                        g_atkOpen[i]  = 1;
                        g_atkStart[i] = now;
                        fprintf(f, "t=%u mode=%s ATK C%d act=%d\n", (unsigned)now, g_mode, i, av);
                        fflush(f);
                    }
                }
                if (g_atkOpen[i] && (g_chr[i].act == 0 || g_chr[i].act == 105 || g_chr[i].act == 11))
                {
                    g_atkOpen[i] = 0;
                    fprintf(f, "t=%u mode=%s ATK-WIN-END C%d hitOnMe=%d\n", (unsigned)now, g_mode, i,
                            ((LONG)g_playerHitMs >= (LONG)g_atkStart[i]) ? 1 : 0);
                    fflush(f);
                }
            }
        }

        // ---- ????????????????????????? ----
        {
            static DWORD lastDiag = 0;
            LONG hit = g_playerHitMs;
            if (hit != (LONG)lastDiag)
            {
                int k;
                lastDiag = (DWORD)hit;
                fprintf(f, "t=%u mode=%s HIT-DIAG ?????:", (unsigned)now, g_mode);
                for (k = 0; k < n; k++)
                    if (g_chr[k].addr != pchr)
                        fprintf(f, " C%d=%d", k, g_chr[k].act);
                fputs("\n", f);
                fflush(f);
                // ????????????????????"???"
                for (k = 0; k < n; k++)
                {
                    int av, j, known = 0;
                    if (g_chr[k].addr == pchr) continue;
                    av = g_chr[k].act;
                    if (av == 0 || av == 105 || av == 11) continue;
                    for (j = 0; j < g_nAtk[k]; j++) if (g_atkVals[k][j] == av) { known = 1; break; }
                    if (!known && g_nAtk[k] < 8)
                    {
                        g_atkVals[k][g_nAtk[k]] = av;
                        g_nAtk[k]++;
                        fprintf(f, "t=%u mode=%s LEARN C%d ???=%d (? %d ?)\n",
                                (unsigned)now, g_mode, k, av, g_nAtk[k]);
                    }
                }
            }
        }

        if (is_slow_mode(g_mode))
        {
            static DWORD entered = 0;
            static DWORD lastIdx = 999;
            static const double steps[4] = {1.00, 0.80, 0.60, 0.40};
            if (entered == 0)
            {
                entered = now;
                fprintf(f, "# CLOCKSWEEP start\n");
                fflush(f);
            }
            DWORD idx = ((now - entered) / 2500) % 4;
            set_clock_speed(steps[idx]);
            if (idx != lastIdx)
            {
                lastIdx = idx;
                fprintf(f, "t=%u mode=%s CLOCKSWEEP speed=%.2f\n", (unsigned)now, g_mode, g_clockSpeed);
                fflush(f);
            }
        }
        else
        {
            // ???? + ?????? fastkey ???(2ms ??)?
            // ????????????????(?? ~500ms)??????????
        }

        // ---- ????: ?????"???? / ????? / safe_read ??" ----
        {
            static DWORD perfT = 0;
            static LONG  perfN = 0;
            static double bodySum = 0.0, bodyMax = 0.0;
            double bodyMs = qms() - bodyT0;
            perfN++;
            bodySum += bodyMs;
            if (bodyMs > bodyMax) bodyMax = bodyMs;
            if (perfT == 0) perfT = now;
            if (now - perfT >= 1000)
            {
                LONG sr = InterlockedExchange(&g_srCalls, 0);
                fprintf(f, "t=%u PERF iters=%ld periodAvg=%.1fms bodyAvg=%.2fms bodyMax=%.2fms safeReads/s=%ld\n",
                        (unsigned)now, perfN,
                        (double)(now - perfT) / (double)(perfN ? perfN : 1),
                        bodySum / (double)(perfN ? perfN : 1), bodyMax, sr);
                fflush(f);
                perfT = now; perfN = 0; bodySum = 0.0; bodyMax = 0.0;
            }
        }

        Sleep(4);
    }
    fputs("# char watch stop\n", f);
    fclose(f);
    return 0;
}

#include "sekiro_tables.h"
#include "sekiro_events.h"     // v78: 我们自己从游戏档案里解出来的 TAE 事件(攻击判定/放弹丸/投技)
#include "sekiro_iframes.h"    // v86: 玩家(c0000)每个动画的无敌帧窗口

// ================= ????????"??????" =================
// ????: thisguymartin/sekiro-deflect-observer ? incoming-coverage.json
//   ????(????????): ChrIns+0x30 -> ChrRes ; ChrRes+0x628 -> signed NpcParam row ID
//   ?ID -> variation (g_npcVar) ; variation/10 -> model
//   (model, variation, animation) -> ???? [startCs, endCs] (??: ??????????)
static unsigned int enemy_variation(unsigned long long ctr, int* npcOut)
{
    // ?????: ???, **????**(??? 1245 ??? NpcParam ?ID) ?? ??????
    unsigned long long chrins = 0, man = 0, p = 0;
    int cand[9], i, k;
    for (i = 0; i < 9; i++) cand[i] = -1;
    if (npcOut) *npcOut = -1;
    if (ctr == 0) return 0;
    rd_ptr(ctr + 0x18, &chrins);                 // ChrIns
    rd_ptr(ctr, &man);                           // container[0] ?????????"Man"
    // ??0: container[0]=Man -> [Man+0x30] -> +0x628   (CT ???)
    if (man != 0 && rd_ptr(man + 0x30, &p) && p != 0) rd_u32(p + 0x628, &cand[0]);
    // ??1: [container[0]+0x68]                        (CT ??? CharacterId)
    if (man != 0) rd_u32(man + 0x68, &cand[1]);
    // ??2: ChrIns+0x30 -> +0x628                      (??????)
    if (chrins != 0 && rd_ptr(chrins + 0x30, &p) && p != 0) rd_u32(p + 0x628, &cand[2]);
    // ??3: ChrIns+0x68
    if (chrins != 0) rd_u32(chrins + 0x68, &cand[3]);
    // ??4: container+0x30 -> +0x628
    if (rd_ptr(ctr + 0x30, &p) && p != 0) rd_u32(p + 0x628, &cand[4]);
    // ??5: ChrIns+0x30 -> +0x624 (ThinkId)
    if (chrins != 0 && rd_ptr(chrins + 0x30, &p) && p != 0) rd_u32(p + 0x624, &cand[5]);
    // ? ?"????? Man"(g_targetMan) ?? ????????? cid=10100020 ?
    if (g_targetMan != 0)
    {
        unsigned long long mp = 0;
        if (rd_ptr(g_targetMan + 0x30, &mp) && mp != 0)
        {
            rd_u32(mp + 0x628, &cand[6]);
            rd_u32(mp + 0x624, &cand[8]);
        }
        rd_u32(g_targetMan + 0x68, &cand[7]);
    }
    for (k = 0; k < 9; k++)
    {
        if (cand[k] <= 0) continue;
        for (i = 0; i < NPCVAR_N; i++)
            if ((int)g_npcVar[i].npcId == cand[k])
            {
                if (npcOut) *npcOut = cand[k];
                return g_npcVar[i].variation;
            }
    }
    // ??????: ???????????(??????????)
    for (k = 0; k < 9; k++) if (cand[k] > 0) { if (npcOut) *npcOut = cand[k]; break; }
    return 0;
}

// ?? 1 ??"????????????????????"(?? = ??? [startCs, endCs])
// ?? v65: ??????"?? 0.45 ?"(ATK_LEAD_CS) ?? ???"????????",
//    ??"????????????????"????????????, ????????
#define S6_LEAD_CS      6       // ????? 0.06 ????"???"(?? 2ms ?? / ????)
#define S6_TAIL_CS      15      // ????? 0.15 ??????
#define S6_DODGE_MS     320     // ???????, "?????"?????(?????? 0.2~0.3s)
#define S6_CONFIRM_MS   350     // ?????????? => ?????????
#define S6_DIST_MIN     2.5f    // ?v31 ????: ??(??????????)
#define S6_DIST_DEF     3.0f    // v87: 还没学到时的默认近战门槛 5.0 -> 3.0
                                //      (近战大多在 2~3 米; 5 米会把长连招的远段也放进来)
#define S6_DIST_FAR     9.0f    // MODE ?? far ?????????
#define S6_DIST_CAP     8.0f    // ???????(????????????, ????;
                                //   ??????, ?????"????????")
#define S6_NEAR_R      30.0f
// ★★ v107: 用户规则 —— "受击之后任何缓速作废"。
//   ① 正在跑的缓速: 一检测到掉血就当场收掉(HIT-CANCEL, v100 起就有);
//   ② 新缓速: 受击后 700ms 内**不允许起任何缓速**(原来只有 400ms,
//      所以"先缓速后受击"还能再蹭到一次突破闪避限制的奖励)。
// ★v108: 用户澄清 —— 只要"缓速期间受击 -> 立刻取消缓速", **不要**受击冷却
//   (加了冷却会让多段连击期间根本没缓速可用)。恢复成原来的 400ms。
#define S6_HITQUIET_MS  400
#define S6_ABORT_MS     400     // ?v31 ????: ???? 400ms ??????

// ????(???? AtkSet ???????)
static int anim_is_learned_attack(int anim);

// ---- ????"????????" ----
// ??????: ???????????, ???????????????
// ???? 1~2m -> ?????????? 2.5m;
// ???? 4m ???? -> ???????? 5m??????????
#define REACH_MAX 24
typedef struct { unsigned int model; float reach; } ReachRec;
static ReachRec g_reach[REACH_MAX];
static volatile LONG g_reachN = 0;
static unsigned long long g_lastHitCtr = 0;   // ?????????????(????????)
static DWORD g_lastHitCtrT = 0;

// ---- "?????"?? ----
// ?/??/??? ??????, ??????????????(????: ? 72% ?
// ?????????)?????"??????", ????: ???? 1.5 ?????,
// ?????? >= 2m (??????????, ?? v31 ??) -> ???????????
#define S6_GRACE_MS   1500
#define S6_GRACE_DIST 2.0f
#define S6_GRACE_MAXD 30.0f
#define S6_GRACE_MAX  8
typedef struct { unsigned long long ctr; DWORD at; int anim; } GraceRec;
static GraceRec g_grace[S6_GRACE_MAX];

// ---- ★v74: 远程(箭/苦无/手里剑)命中点的自学表 ----
// 社区那张表对远程是空的(含 BulletBehavior 的动画被他们标成 unverified 排除了),
// 所以远程只能自己学:
//   每当有东西从 >=5 米外打中我们, 就记下"射手当时在播哪个动画、动画播到第几秒"
//   (箭飞到你身上这一刻, 射手的状态和它"开弓->松手->箭飞行"是固定的).
//   之后只要有同样的 (敌人类型, 动画, 动画时间) 出现, 又按了闪避 -> 就是躲这一箭。
// 关键: 学的是"被箭打中"时的状态, 但躲开的那一箭到达时间完全一样, 所以照样适用。
// ★v76: 采样要带距离。箭飞 3 米和飞 12 米, 到达时射手的动画时间差很多;
//        只按"动画时间"匹配 -> 只有学过的那个距离附近才认得出(这就是"离得近才缓速")。
//        每个 (敌人类型, 动画) 存最多 3 个 (距离, 动画时间) 样本, 匹配时按当前距离插值。
#define RNG_MAX 24
#define RNG_S 3
typedef struct { unsigned int model; int anim; int n; float d[RNG_S]; int cs[RNG_S]; } RngRec;
static RngRec g_rng[RNG_MAX];
static volatile LONG g_rngUsed = 0;

static void rng_note(unsigned int model, int anim, int cs, float dist)
{
    int i, j;
    if (model == 0 || anim < 0 || cs < 0 || dist < 0.0f) return;
    for (i = 0; i < RNG_MAX; i++)
        if (g_rng[i].model == model && g_rng[i].anim == anim)
        {
            // 已经有两个以上样本且这个距离夹在中间 -> 替换最近的那个, 保持样本覆盖不同距离
            if (g_rng[i].n >= RNG_S)
            {
                int best = 0; float bd = 1e9f;
                for (j = 0; j < RNG_S; j++)
                {
                    float dd = g_rng[i].d[j] - dist; if (dd < 0) dd = -dd;
                    if (dd < bd) { bd = dd; best = j; }
                }
                g_rng[i].d[best] = dist; g_rng[i].cs[best] = cs;
            }
            else
            {
                g_rng[i].d[g_rng[i].n] = dist; g_rng[i].cs[g_rng[i].n] = cs; g_rng[i].n++;
            }
            return;
        }
    for (i = 0; i < RNG_MAX; i++)
        if (g_rng[i].model == 0)
        {
            g_rng[i].model = model; g_rng[i].anim = anim; g_rng[i].n = 1;
            g_rng[i].d[0] = dist; g_rng[i].cs[0] = cs;
            InterlockedIncrement(&g_rngUsed);
            return;
        }
}

// 按当前距离推算"箭到达时应有的动画时间", 再看现在是不是那个状态
static int rng_match(unsigned int model, int anim, int cs, float dist)
{
    int i, j;
    if (model == 0 || anim < 0 || cs < 0) return 0;
    for (i = 0; i < RNG_MAX; i++)
        if (g_rng[i].model == model && g_rng[i].anim == anim)
        {
            int expect, tol;
            // ★v86: 距离也要接近 —— 只认"和观测到的那次距离差不多(±3 米)"的情况。
            //   不然近战学到的一个点会被外推到远处(飞渡浮舟那次空放触发就是这么来的)。
            if (dist >= 0.0f)
            {
                float best = 1e9f;
                for (j = 0; j < g_rng[i].n; j++)
                {
                    float dd = g_rng[i].d[j] - dist;
                    if (dd < 0) dd = -dd;
                    if (dd < best) best = dd;
                }
                if (best > g_rngTol) return 0;   // ★v109: 容差/MODE 可调(默认 3.0 米)
            }
            if (g_rng[i].n == 1 || dist < 0.0f)
            {
                expect = g_rng[i].cs[0]; tol = 35;
            }
            else
            {
                // 找距离上最靠近当前距离的两个样本, 线性插值(外推也给)
                int a = 0, b = 1;
                if (g_rng[i].n > 2)
                {
                    float m = 1e9f;
                    for (j = 0; j < g_rng[i].n; j++)
                        for (int k = j + 1; k < g_rng[i].n; k++)
                        {
                            float lo = g_rng[i].d[j] < g_rng[i].d[k] ? g_rng[i].d[j] : g_rng[i].d[k];
                            float hi = g_rng[i].d[j] < g_rng[i].d[k] ? g_rng[i].d[k] : g_rng[i].d[j];
                            float pen = (dist < lo) ? (lo - dist) : ((dist > hi) ? (dist - hi) : 0.0f);
                            if (pen < m) { m = pen; a = j; b = k; }
                        }
                }
                {
                    float d1 = g_rng[i].d[a], d2 = g_rng[i].d[b];
                    if (d1 == d2) expect = g_rng[i].cs[a];
                    else expect = (int)((float)g_rng[i].cs[a] +
                                 (float)(g_rng[i].cs[b] - g_rng[i].cs[a]) * (dist - d1) / (d2 - d1) + 0.5f);
                }
                tol = 30;
            }
            return (cs >= expect - tol && cs <= expect + tol) ? 1 : 0;
        }
    return 0;
}

static void grace_note(unsigned long long ctr, int anim, DWORD now)
{
    int i;
    if (ctr == 0) return;
    for (i = 0; i < S6_GRACE_MAX; i++)
        if (g_grace[i].ctr == ctr)
        {
            g_grace[i].at = now; g_grace[i].anim = anim;
            return;
        }
    for (i = 0; i < S6_GRACE_MAX; i++)
        if (g_grace[i].ctr == 0 || (now - g_grace[i].at) > 10000)
        {
            g_grace[i].ctr = ctr; g_grace[i].at = now; g_grace[i].anim = anim;
            return;
        }
}

static int grace_recent(unsigned long long ctr, DWORD now)
{
    int i;
    if (ctr == 0) return 0;
    for (i = 0; i < S6_GRACE_MAX; i++)
        if (g_grace[i].ctr == ctr && (now - g_grace[i].at) <= S6_GRACE_MS)
            return g_grace[i].anim;
    return -1;
}

static void reach_note(unsigned int model, float d)
{
    int i;
    LONG n = g_reachN;
    if (model == 0 || d < 0.0f) return;
    for (i = 0; i < n && i < REACH_MAX; i++)
        if (g_reach[i].model == model)
        {
            if (d > g_reach[i].reach) g_reach[i].reach = d;
            return;
        }
    if (n < REACH_MAX)
    {
        g_reach[n].model = model;
        g_reach[n].reach = d;
        InterlockedIncrement(&g_reachN);
    }
}

static float reach_gate(unsigned int model, int forceFar)
{
    int i;
    float g;
    if (forceFar) return S6_DIST_FAR;
    for (i = 0; i < g_reachN && i < REACH_MAX; i++)
        if (g_reach[i].model == model)
        {
            g = g_reach[i].reach + 1.0f;
            if (g < S6_DIST_MIN) g = S6_DIST_MIN;
            if (g > S6_DIST_CAP) g = S6_DIST_CAP;
            return g;
        }
    return S6_DIST_DEF;
}

// ★★ v89: 按"具体招式"记射程。
//   只用敌人级射程会出问题: 某一招(比如突进/长枪)在 3.15 米打中过你, 门槛就涨到 4.15 米,
//   于是那个敌人所有招的 3~4 米空放闪避都被当成"够得着" —— 你报的
//   "离得近而不会被砍中的距离空放闪避还是会缓速"就是这么来的。
//   现在按 (敌人类型, 动画) 分别记: 长枪那一招自己放宽, 普通挥砍维持 2.5~3 米。
#define REACH2_MAX 40
typedef struct { unsigned int model; int anim; float reach; } ReachRec2;
static ReachRec2 g_reach2[REACH2_MAX];
static volatile LONG g_reachN2 = 0;

static void reach_note2(unsigned int model, int anim, float d)
{
    int i;
    LONG n = g_reachN2;
    if (model == 0 || anim < 0 || d < 0.0f) return;
    for (i = 0; i < n && i < REACH2_MAX; i++)
        if (g_reach2[i].model == model && g_reach2[i].anim == anim)
        {
            if (d > g_reach2[i].reach) g_reach2[i].reach = d;
            return;
        }
    if (n < REACH2_MAX)
    {
        g_reach2[n].model = model; g_reach2[n].anim = anim; g_reach2[n].reach = d;
        InterlockedIncrement(&g_reachN2);
    }
}

static float reach_gate2(unsigned int model, int anim, int forceFar)
{
    int i;
    float g;
    if (forceFar) return S6_DIST_FAR;
    for (i = 0; i < g_reachN2 && i < REACH2_MAX; i++)
        if (g_reach2[i].model == model && g_reach2[i].anim == anim)
        {
            g = g_reach2[i].reach + g_reachMargin;   // ★v109: 余量/MODE 可调
            if (g < S6_DIST_MIN) g = S6_DIST_MIN;
            if (g > g_reachCap) g = g_reachCap;      // ★v109: 上限/MODE 可调
            return g;
        }
    // 没有这一招的记录: 用敌人级(如果有), 否则默认
    g = reach_gate(model, 0);
    if (g > S6_DIST_DEF) g = S6_DIST_DEF;
    return g;
}

// ---- ???????? ----
// chrwatch ? 1 ???????"????", ?????? ?? ? boss ?????
// ??????????, ??"??????"???????? chrwatch ???
// ???(g_chr/g_ctrOf)?????, ? 250ms ??, ???? 4 ??
static unsigned long long g_s6Near[4];
static volatile LONG g_s6NearN = 0;
static DWORD g_s6NearAt = 0;

static void s6_refresh_near(DWORD now)
{
    int i, nb = 0;
    float pp[3], best[4];
    unsigned long long pctr, pchr;
    if (now - g_s6NearAt < 250) return;
    g_s6NearAt = now;
    g_s6NearN = 0;
    pctr = ctr_of_player();
    pchr = player_chr();
    if (pctr == 0 || !ctr_pos(pctr, pp)) return;
    for (i = 0; i < g_nchr && i < MAX_CHR; i++)
    {
        float ep[3], d, dx, dy, dz;
        if (g_chr[i].addr == 0 || g_chr[i].addr == pchr) continue;
        if (g_ctrOf[i] == 0) g_ctrOf[i] = find_container(g_chr[i].addr);
        if (g_ctrOf[i] == 0) continue;
        if (!ctr_pos(g_ctrOf[i], ep)) continue;
        dx = ep[0]-pp[0]; dy = ep[1]-pp[1]; dz = ep[2]-pp[2];
        d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d > S6_NEAR_R) continue;
        {
            int j = nb;
            while (j > 0 && best[j-1] > d) { best[j] = best[j-1]; g_s6Near[j] = g_s6Near[j-1]; j--; }
            best[j] = d; g_s6Near[j] = g_ctrOf[i];
            if (nb < 4) nb++;
        }
    }
    g_s6NearN = nb;
}

// ?? <-> ???? ???(?); ????? -1
static float player_enemy_dist(void)
{
    unsigned long long p = ctr_of_player(), e = enemy_ctr_best();
    float a[3], b[3];
    if (p == 0 || e == 0) return -1.0f;
    if (!ctr_pos(p, a) || !ctr_pos(e, b)) return -1.0f;
    {
        float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }
}

// ---- ????: (model, variation, anim) -> ???? [startCs, endCs] ----
// pass0 = ? variation ????(???? -1, ?"??"), pass1 = ????
static int atk_window(unsigned int model, unsigned int var, int anim, int* oS, int* oE)
{
    int pass, i;
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < ATKREC_N; i++)
        {
            if (g_atkRec[i].model != model) continue;
            if (pass == 0) { if (g_atkRec[i].variation != var) continue; }
            else           { if (g_atkRec[i].variation != 0)   continue; }
            if ((unsigned int)g_atkRec[i].anim != (unsigned int)anim) continue;
            if (oS) *oS = (int)g_atkRec[i].startCs;
            if (oE) *oE = (int)g_atkRec[i].endCs;
            return 1;
        }
    }
    return 0;
}

// ---- ?? -> (variation, model) ?? ----
// ???????????? 9 ? NpcParam ???, ????????????
// "??????" ?? false ?? ????"???????????"?
// ????????????, ???????, ?????????????
// v77: 这一招在表里是不是 mikiri(突刺危, resp==2)。
//   要的手感: 普通闪避按 v72 那样"按下闪避瞬间"就缓速;
//   只有识破(突刺危)走"按下 -> 待确认 -> 这一击被吃掉才算"那条(v76 那条)。
static int anim_is_mikiri(unsigned int model, int anim)
{
    int i;
    if (anim < 0) return 0;
    for (i = 0; i < ATKREC_N; i++)
    {
        if (g_atkRec[i].model != model) continue;
        if ((unsigned int)g_atkRec[i].anim != (unsigned int)anim) continue;
        if (g_atkRec[i].resp == 2) return 1;      // 2 = mikiri(突刺危)
    }
    return 0;
}

// ★v78: 我们自己从游戏档案里解出来的 TAE 事件查表(sekiro_events.h, 已按 model/anim/type/start 排序)
//   type 1 = 攻击判定(hitbox) 的真正时间窗(比社区那张"相位表"准, 是他们文档里承认的
//            "activation, not contact" 之外的真数据)
//   type 2 = 放弹丸(BulletBehavior) —— 箭/苦无/手里剑在这一刻离手
//   type 304 = 投技
//   返回 1 表示找到; 同一 (model,anim,type) 可能有多条(连段), 这里给出覆盖 cs 的那条,
//   找不到覆盖的则给出最靠近 cs 的一条(调用方自己决定容差)
static int ev_find(unsigned int model, int anim, int type, int cs,
                   unsigned short* oS, unsigned short* oE, short* oJudge)
{
    int i, best = -1, bestD = 0x7fffffff;
    if (anim < 0) return 0;
    for (i = 0; i < EV_N; i++)
    {
        int d;
        if (g_ev[i].model != model) continue;
        if ((int)g_ev[i].anim != anim) continue;
        if ((int)g_ev[i].type != type) continue;
        if (oS) *oS = g_ev[i].startCs;
        if (oE) *oE = g_ev[i].endCs;
        if (oJudge) *oJudge = g_ev[i].judge;
        if (cs >= (int)g_ev[i].startCs && cs <= (int)g_ev[i].endCs) return 1;
        d = (cs < (int)g_ev[i].startCs) ? ((int)g_ev[i].startCs - cs) : (cs - (int)g_ev[i].endCs);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) return 0;
    if (oS) *oS = g_ev[best].startCs;
    if (oE) *oE = g_ev[best].endCs;
    if (oJudge) *oJudge = g_ev[best].judge;
    return 1;
}

// ★v86: 玩家当前动画的无敌帧窗口(数据来自 c0000 的 TAE: sekiro_iframes.h, 按 anim 升序)
static int ifr_find(int anim, unsigned short* oS, unsigned short* oE)
{
    int lo = 0, hi = IFR_N - 1;
    if (anim < 0) return 0;
    while (lo <= hi)
    {
        int mid = (lo + hi) >> 1;
        if (g_ifr[mid].anim == anim)
        {
            if (oS) *oS = g_ifr[mid].s;
            if (oE) *oE = g_ifr[mid].e;
            return 1;
        }
        if (g_ifr[mid].anim < anim) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

#define EMC_MAX 8
typedef struct { unsigned long long ctr; unsigned int var, model; DWORD at; } EmCache;
static EmCache g_emc[EMC_MAX];
static volatile LONG g_emcN = 0;

static unsigned int model_of_ctr(unsigned long long ctr, unsigned int* varOut, int* npcOut, DWORD now)
{
    int i;
    LONG n = g_emcN;
    if (ctr == 0) return 0;
    for (i = 0; i < n && i < EMC_MAX; i++)
        if (g_emc[i].ctr == ctr && (now - g_emc[i].at) < 15000)
        {
            if (varOut) *varOut = g_emc[i].var;
            if (npcOut) *npcOut = -1;
            return g_emc[i].model;
        }
    {
        int npc = -1;
        unsigned int v = enemy_variation(ctr, &npc);
        if (npcOut) *npcOut = npc;
        if (v != 0)
        {
            unsigned int m = v / 10;
            for (i = 0; i < n && i < EMC_MAX; i++)
                if (g_emc[i].ctr == ctr)
                {
                    g_emc[i].var = v; g_emc[i].model = m; g_emc[i].at = now;
                    break;
                }
            if (i == n && n < EMC_MAX)
            {
                g_emc[n].ctr = ctr; g_emc[n].var = v; g_emc[n].model = m; g_emc[n].at = now;
                InterlockedIncrement(&g_emcN);
            }
            if (varOut) *varOut = v;
            return m;
        }
    }
    return 0;
}

// ---- ??????"??????"??? ----
// ???? enemy_ctr_best(): ????"????", ???????; ?"????"
// (g_nearCtr) ? chrwatch ????, ????????????, ????????
// ??????? ?? ??"??????"????, ????????
typedef struct {
    unsigned long long ctr;
    unsigned int  var, model;
    int           anim, cs, ws, we;
    int           isAtk;      // 0=?????? 1=??????? 2=???????
    int           inWin;      // ?????????????(???????)
    int           oor;        // 1 = ??, ????
    float         dist;
} S6Cand;
#define S6_CAND_MAX 6

// ?? v67 = v31 ???: ??"??????????????????" + "??"
//    ????????????? ?? v31 ????????????????
// ★★ v104: "按下闪避那一刻的距离"快照。
//   用户报:"好多招都得原地闪才触发, 往左右闪就不触发" —— 原因就是往左右闪
//   会把玩家带走一段距离, 等这一击结算时距离已经变远, 于是 rng_match 的
//   "±3 米"和 reach_gate2 的射程一道把这次闪避判掉了。
//   但判"这一下能不能被垫步躲开"应该看**按闪避那一瞬间**的距离, 不是闪完之后的。
static volatile unsigned long long g_pressCtr[6];
static volatile float g_pressD[6];
static volatile DWORD g_pressAt = 0;

static void press_snapshot(DWORD now)
{
    unsigned long long pc = ctr_of_player();
    unsigned long long list[8];
    float pa[3];
    int ln = 0, n = 0, i;
    g_pressAt = now;
    for (i = 0; i < 6; i++) { g_pressCtr[i] = 0; g_pressD[i] = 0.0f; }
    if (pc == 0 || !ctr_pos(pc, pa)) return;
    if (g_targetMan != 0)
    {
        unsigned long long t2 = 0;
        if (rd_ptr(g_targetMan + 0x1FF8, &t2) && t2 != 0) list[ln++] = t2;
    }
    if (g_nearCtr != 0 && ln < 8) list[ln++] = g_nearCtr;
    for (i = 0; i < g_s6NearN && ln < 8; i++)
        if (g_s6Near[i] != 0) list[ln++] = g_s6Near[i];
    for (i = 0; i < ln && n < 6; i++)
    {
        float b[3];
        float dx, dy, dz;
        if (!ctr_pos(list[i], b)) continue;
        dx = b[0] - pa[0]; dy = b[1] - pa[1]; dz = b[2] - pa[2];
        g_pressCtr[n] = list[i];
        g_pressD[n] = sqrtf(dx*dx + dy*dy + dz*dz);
        n++;
    }
}

static float press_dist_of(unsigned long long ctr)
{
    int i;
    for (i = 0; i < 6; i++) if (g_pressCtr[i] == ctr) return g_pressD[i];
    return 0.0f;
}

static int s6_scan(S6Cand* out, int maxOut, DWORD now,
                    unsigned long long* lastAtk, DWORD* lastAtkT,
                    unsigned long long lastHitter, int forceFar, int* nOut)
{
    unsigned long long cand[10];
    int n = 0, k, i, filled = 0, any = 0;
    unsigned long long tgt = 0, pc = ctr_of_player();
    float pa[3];
    int havePa = (pc != 0) && ctr_pos(pc, pa);

    if (g_targetMan != 0) rd_ptr(g_targetMan + 0x1FF8, &tgt);
    if (tgt != 0) cand[n++] = tgt;
    if (g_nearCtr != 0)
    {
        for (i = 0; i < n; i++) if (cand[i] == g_nearCtr) break;
        if (i == n) cand[n++] = g_nearCtr;
    }
    if (lastAtk != NULL && *lastAtk != 0 && lastAtkT != NULL && (now - *lastAtkT) < 1500)
    {
        for (i = 0; i < n; i++) if (cand[i] == *lastAtk) break;
        if (i == n) cand[n++] = *lastAtk;
    }
    // ?v68: ????????????(???????)
    if (lastHitter != 0)
    {
        for (i = 0; i < n; i++) if (cand[i] == lastHitter) break;
        if (i == n && n < 9) cand[n++] = lastHitter;
    }
    // ?v68: chrwatch ????"??? 4 ?"(? 250ms ??)
    for (i = 0; i < g_s6NearN && i < 4 && n < 9; i++)
    {
        unsigned long long c0 = g_s6Near[i];
        int j;
        if (c0 == 0) continue;
        for (j = 0; j < n; j++) if (cand[j] == c0) break;
        if (j == n) cand[n++] = c0;
    }

    for (k = 0; k < n && filled < maxOut; k++)
    {
        S6Cand* c = &out[filled];
        unsigned long long ob = 0;
        float t = 0.0f, b[3];
        int npc = -1;
        c->ctr = cand[k]; c->var = 0; c->model = 0;
        c->anim = -1; c->cs = -1; c->ws = 0; c->we = 0;
        c->isAtk = 0; c->inWin = 0; c->oor = 0; c->dist = -1.0f;
        c->model = model_of_ctr(cand[k], &c->var, &npc, now);
        if (c->model == 0) continue;
        if (!rd_ptr(cand[k] + 0x10, &ob) || ob == 0) continue;
        rd_u32(ob + 0x20, &c->anim);
        if (c->anim < 0) continue;
        if (!safe_read(ob + 0x24, &t, 4)) continue;
        c->cs = (int)(t * 100.0f + 0.5f);
        if (havePa && ctr_pos(cand[k], b))
        {
            float dx = b[0]-pa[0], dy = b[1]-pa[1], dz = b[2]-pa[2];
            c->dist = sqrtf(dx*dx + dy*dy + dz*dz);
            // ★★ v104: 用"按下闪避那一刻"的距离(如果更近)来判 —— 往左右闪也能触发
            if ((DWORD)(now - g_pressAt) <= 600)
            {
                float pd = press_dist_of(cand[k]);
                if (pd > 0.0f && pd < c->dist) c->dist = pd;
            }
        }
        filled++;
        if (atk_window(c->model, c->var, c->anim, &c->ws, &c->we)) c->isAtk = 1;
        else if (anim_is_learned_attack(c->anim))                   c->isAtk = 2;
        if (c->isAtk == 0)
        {
            // ★v79: 社区表不认识这个动画, 但我们自己从游戏档案解出来的事件表认识它
            //   (典型: 弦一郎的拉弓动画 3023 —— 社区把含子弹的动画整类排除了)。
            //   以前这里直接 continue, 于是"档案放弹丸/档案判定帧"根本没有候选可用,
            //   远箭就只能偶尔碰到社区表里那几个动画才触发。
            unsigned short s2 = 0, e2 = 0; short j2 = 0;
            if (ev_find(c->model, c->anim, 1, c->cs, &s2, &e2, &j2) ||
                ev_find(c->model, c->anim, 2, c->cs, &s2, &e2, &j2))
                c->isAtk = 3;                 // 3 = 只有我们自己的档案数据认识
        }
        if (c->isAtk == 0) continue;
        // v73: 先记下"他刚出过招"再判距离. 以前是"超出射程就 continue",
        //      于是远处放箭的射手从来没被登记, 远程宽限永远拿不到数据.
        grace_note(cand[k], c->anim, now);
        if (c->dist >= 0.0f && c->dist > reach_gate2(c->model, c->anim, forceFar))
        {
            // ★v82: 以前这里直接 continue -> "超出近战射程"的敌人(含远处放箭的射手)
            //   会从候选里消失, 于是"档案放弹丸"根本看不到它(实测 8.9m 的箭就被这样漏掉)。
            //   现在保留在候选里(oor=1), 只是近战通道不许用它; 放弹丸/学到的命中状态仍然可用。
            c->oor = 1;
            continue;
        }
        if (c->isAtk == 1 && c->cs >= c->ws - S6_LEAD_CS && c->cs <= c->we + S6_TAIL_CS) c->inWin = 1;
        any = 1;
        if (lastAtk) *lastAtk = cand[k];
        if (lastAtkT) *lastAtkT = now;
    }
    if (nOut) *nOut = filled;
    return any;
}

static int enemy_attack_phase(unsigned long long ctr, unsigned int* oModel, unsigned int* oAnim,
                              int* oT_cs, int* oRemain_cs, unsigned int* oVariation, int* oNpc,
                              int leadCs, int* oStart_cs, int* oEnd_cs)
{
    unsigned long long ob = 0;
    int   npc = -1, anim = -1, cs = -1, s = 0, e = 0;
    float t = 0.0f;
    unsigned int var, model;
    if (oModel) *oModel = 0;
    if (oAnim)  *oAnim = 0;
    if (oT_cs)  *oT_cs = -1;
    if (oRemain_cs) *oRemain_cs = 0;
    if (oStart_cs) *oStart_cs = 0;
    if (oEnd_cs) *oEnd_cs = 0;
    var = enemy_variation(ctr, &npc);
    if (oVariation) *oVariation = var;
    if (oNpc) *oNpc = npc;
    if (var == 0) return 0;
    model = var / 10;
    if (oModel) *oModel = model;
    if (!rd_ptr(ctr + 0x10, &ob) || ob == 0) return 0;
    rd_u32(ob + 0x20, &anim);
    if (anim < 0) return 0;
    if (!safe_read(ob + 0x24, &t, 4)) return 0;
    cs = (int)(t * 100.0f + 0.5f);
    if (oAnim) *oAnim = anim;
    if (oT_cs) *oT_cs = cs;
    if (!atk_window(model, var, anim, &s, &e)) return 0;
    if (oStart_cs) *oStart_cs = s;
    if (oEnd_cs) *oEnd_cs = e;
    if (cs >= s - leadCs && cs <= e + S6_TAIL_CS)
    {
        if (oRemain_cs) *oRemain_cs = e - cs;
        return 1;
    }
    return 0;
}

#define SLOWPATH    P_SLOW()
#define ANIM_FRESH_MS  700      // "???????"???
#define ABORT_MS       400      // ??????, ???????????
#define FAST_MS        400      // 1act ??: ???????????????

static FILE* g_slowF = NULL;
static volatile LONG g_abortActive = 0;
static DWORD g_abortUntil = 0;
static LONG  g_abortHit = 0;
static volatile LONG g_slowCount = 0, g_abortCount = 0;

// ================= ★v96: 缓速窗口的"凸面镜"特效 =================
//   实现整个放在 fx_glow.h 里 —— 同一份代码有一个离线冒烟测试(fx_smoke.exe),
//   用真实 D3D11 验过: 能挂上 Present、HLSL 能编译、画完以后角落真的变暗。
//   这里只负责: 什么时候画(FX_ACTIVE)、淡入淡出(fx_tick)、日志。
//   MODE 里写 nofx 可以整体关掉; 它自己在任何一步出错时也会永久关掉自己。
static void fx_tick(void);
static volatile LONG g_sfxOn = 1;       // MODE: nosfx -> 0(关掉提示音)
static void fx_sting(void);
static void fx_sting_stop(void);   // ★v126/127: 缓速结束 -> 平滑淡出提示音
static volatile DWORD g_stingFadeAt = 0; static volatile LONG g_stingFading = 0;
static volatile DWORD g_slowOffAt = 0;  // ★v101: 窗口结束的时刻(给"柔和淡出"用)
static volatile float g_fadeFrom = 0.0f;// ★v101: 结束那一刻的强度

// ================= ★v99: 游戏音效缓速(FMOD) =================
//  ※ 只狼的音频**不是 XAudio2**, 是 FMOD Ex —— 游戏目录里就是 fmodex64.dll。
//  FMOD 提供 FMOD_ChannelGroup_SetPitch: 对 master channel group 调一次,
//  整个游戏的音频一起降调/拉长(不是外挂音效, 是游戏自己的声音)。
//  捕获 FMOD_SYSTEM*: FMOD_System_Update 每帧都被游戏调用, 第一个参数就是它。
static volatile unsigned long long g_fmodSys = 0;
static volatile LONG  g_fmodOn        = 1;      // MODE: nofmod -> 0
static volatile float g_fmodPitchWant = 0.65f;  // 缓速时音频降到多少(1.0 = 不变)
static volatile float g_fmodPitchNow  = 1.0f;   // 现在实际设的值
static volatile LONG  g_fmodSetsOld = 0;   // (v124: 实际用的 g_fmodSets 提前声明了)
static volatile LONG  g_fmodHookMode  = 0;      // 1=IAT 2=代码补丁 0=没挂上

static int (WINAPI *g_fmodUpdateReal)(void*, void*, void*, void*) = NULL;
static void fmod_on_update(void* sys);      // ★v103: 前向声明(定义在后面)
static int WINAPI my_fmod_update(void* a1, void* a2, void* a3, void* a4)
{
    if (a1 != NULL && g_fmodSys == 0)
        g_fmodSys = (unsigned long long)(uintptr_t)a1;
    if (a1 != NULL) fmod_on_update(a1);     // ★v103: 兜底路径也在游戏线程里干活
    return g_fmodUpdateReal(a1, a2, a3, a4);
}

// 在目标附近 ±2GB 里找可执行内存(给 6 字节 detour 用)
static void* fx_alloc_near(void* target, unsigned long long sz)
{
    unsigned long long t = (unsigned long long)(uintptr_t)target;
    unsigned long long off;
    for (off = 0x01000000ULL; off < 0x70000000ULL; off += 0x01000000ULL)
    {
        void* p;
        p = VirtualAlloc((LPVOID)(uintptr_t)(t - off), (SIZE_T)sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
        p = VirtualAlloc((LPVOID)(uintptr_t)(t + off), (SIZE_T)sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

// 给 fmodex64.dll 里某个导出函数打 6~9 字节 detour:
//   先核对开头的机器码, 对不上就放弃(绝不硬来); 被覆盖的指令在 stub 里原样重放。
//   ★v100: 关键修正 —— 只狼是 C++, 它调的是 `?update@System@FMOD@@...`(C++ 方法),
//   不是 C 包装函数 FMOD_System_Update, 所以上一版补丁挂上了却从来没被调用(sys=0)。
static bool fmod_code_hook_ex(const char* fn, const unsigned char* expect, int n)
{
    HMODULE h;
    unsigned char* fp;
    unsigned char* stub;
    unsigned char orig[16];
    DWORD oldp = 0;
    long rel;
    int i;
    if (n < 5 || n > 14) return false;
    h = GetModuleHandleA("fmodex64.dll");
    if (h == NULL) h = LoadLibraryA("fmodex64.dll");
    if (h == NULL) return false;
    fp = (unsigned char*)GetProcAddress(h, fn);
    if (fp == NULL) return false;
    if (!safe_read((unsigned long long)(uintptr_t)fp, orig, n)) return false;
    if (memcmp(orig, expect, n) != 0) return false;
    stub = (unsigned char*)fx_alloc_near(fp, 96);
    if (stub == NULL) return false;
    {
        unsigned char* d = stub;
#define EMIT_RAX_ADDR(a) do { d[0]=0x48; d[1]=0xB8; *(unsigned long long*)(d+2)=(unsigned long long)(uintptr_t)(a); d+=10; } while(0)
        EMIT_RAX_ADDR(&g_fmodSys); d[0]=0x48; d[1]=0x89; d[2]=0x08; d+=3;   // mov [rax],rcx
        memcpy(d, orig, n); d += n;                                        // 原样重放被覆盖的指令
        EMIT_RAX_ADDR(fp + n); d[0]=0xFF; d[1]=0xE0; d+=2;                 // jmp rax
#undef EMIT_RAX_ADDR
        FlushInstructionCache(GetCurrentProcess(), stub, (SIZE_T)(d - stub));
    }
    rel = (long)(stub - (fp + 5));
    if (!VirtualProtect(fp, (SIZE_T)n, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    suspend_others(true);
    fp[0] = 0xE9;
    *(long*)(fp + 1) = rel;
    for (i = 5; i < n; i++) fp[i] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), fp, (SIZE_T)n);
    suspend_others(false);
    VirtualProtect(fp, (SIZE_T)n, oldp, &oldp);
    return true;
}

static bool fmod_code_hook(void)
{
    // (a) C++ 方法 System::update(游戏每帧都调) —— 头 9 字节原样
    static const unsigned char upd[9] = { 0x48,0x83,0xEC,0x28, 0x48,0x8D,0x54,0x24,0x38 };
    // (b) C 包装 FMOD_System_Update —— 头 6 字节
    static const unsigned char cup[6] = { 0x33,0xC0, 0x4C,0x8D,0x41,0x08 };
    if (fmod_code_hook_ex("?update@System@FMOD@@QEAA?AW4FMOD_RESULT@@XZ", upd, 9)) return true;
    if (fmod_code_hook_ex("FMOD_System_Update", cup, 6)) return true;
    return false;
}

static bool fmod_update_hook(void);   // ★v103 前置声明(定义在后面)
static void fmod_install(void)
{
    HMODULE me = GetModuleHandleA(NULL);
    void* orig = NULL;
    // ★v103: 首选 —— 在游戏自己的线程里干活的 update 钩子
    if (fmod_update_hook())
    {
        g_fmodHookMode = 5;
        if (g_slowF) { fprintf(g_slowF, "t=%u FMOD: 挂上 System::update(所有 FMOD 调用都在游戏线程)\n", (unsigned)GetTickCount()); fflush(g_slowF); }
        return;
    }
    // ★v100: 先试 C++ 方法名(游戏真正调的那个)
    if (hook_iat(me, "?update@System@FMOD@@QEAA?AW4FMOD_RESULT@@XZ", (void*)&my_fmod_update, &orig))
    {
        g_fmodUpdateReal = (int (WINAPI*)(void*,void*,void*,void*))orig;
        g_fmodHookMode = 3;
        if (g_slowF) { fprintf(g_slowF, "t=%u FMOD: 挂上了 IAT(System::update)\n", (unsigned)GetTickCount()); fflush(g_slowF); }
        return;
    }
    if (hook_iat(me, "FMOD_System_Update", (void*)&my_fmod_update, &orig))
    {
        g_fmodUpdateReal = (int (WINAPI*)(void*,void*,void*,void*))orig;
        g_fmodHookMode = 1;
        if (g_slowF) { fprintf(g_slowF, "t=%u FMOD: 挂上了 IAT(FMOD_System_Update)\n", (unsigned)GetTickCount()); fflush(g_slowF); }
        return;
    }
    if (fmod_code_hook())
    {
        g_fmodHookMode = 4;
        if (g_slowF) { fprintf(g_slowF, "t=%u FMOD: IAT 里没有, 改用代码补丁挂上(System::update)\n", (unsigned)GetTickCount()); fflush(g_slowF); }
        return;
    }
    g_fmodHookMode = 0;
    if (g_slowF) { fprintf(g_slowF, "t=%u FMOD: 两种办法都没挂上 -> 音效缓速不可用(其它功能不受影响)\n", (unsigned)GetTickCount()); fflush(g_slowF); }
}

// ---- FMOD API 表(提前声明, 供 v103 的代码使用; 原来的定义在下面被 #if 0 掉) ----
static void fmod_load_api(void);
static bool fmod_update_hook(void);
#define FMOD_MAXCH 96
static void*  g_fch[FMOD_MAXCH];
static float  g_fchFreq[FMOD_MAXCH];
static int    g_fchN = 0;
static volatile LONG g_fchSlowN = 0;
static volatile LONG g_fchDiag[8];
static volatile float g_fmodOutRate = 44100.0f;
typedef int (WINAPI *F_GetNumCh_t)(void*, int*);
typedef int (WINAPI *F_GetCh_t)(void*, int, void**);
typedef int (WINAPI *F_GetFreq_t)(void*, float*);
typedef int (WINAPI *F_SetFreq_t)(void*, float);
typedef int (WINAPI *F_GetNumG_t)(void*, int*);
typedef int (WINAPI *F_GetG_t)(void*, int, void**);
typedef int (WINAPI *F_OverrideFreq_t)(void*, float);
typedef int (WINAPI *F_IsPlaying_t)(void*, int*);
typedef int (WINAPI *F_GetSoftwareFormat_t)(void*, int*, void*, int*);
static F_GetNumCh_t f_getNumCh = NULL;
static F_GetCh_t    f_getCh = NULL;
static F_GetFreq_t  f_getFreq = NULL;
static F_SetFreq_t  f_setFreq = NULL;
static F_GetNumG_t  f_getNumG = NULL;
static F_GetG_t     f_getG = NULL;
static F_OverrideFreq_t f_ovrFreq = NULL;
static F_IsPlaying_t f_isPlaying = NULL;
static void*        f_sysGetChannel = NULL;
static void*        f_sysGetChansPlaying = NULL;
static F_GetSoftwareFormat_t f_getSwFmt = NULL;

// ================= ★v103: 所有 FMOD 调用都搬到游戏自己的线程里 =================
//  v102 卡退的原因(结合离线探针的结论):
//   ① 我们在**自己的线程**里调 FMOD —— 和游戏的音频/update 线程并发, 这是 FMOD 最容易崩的用法;
//   ② 又去递归遍历"子组"(GetNumGroups/GetGroup), 组指针一旦是空的/过期的就会踩空。
//  探针(fmod_probe.exe, 用自己的进程调同一个 fmodex64.dll)证明:
//   - System_GetChannel / GetChannel / GetFrequency / SetFrequency 都正常;
//   - **ChannelGroup::OverrideFrequency(master, 采样率×ratio) 真的有用**:
//     一调, 正在播的那个声道频率就从 44100 变成 19200。
//  所以现在: 我们的线程只写一个"我想要多少"的数字, 真正的 FMOD 调用全部放进
//  `FMOD::System::update` 的钩子里(游戏线程, 每帧一次) —— 顺序、线程都跟游戏一致。
static volatile float g_fmodWant = 1.0f;      // 我们的线程请求的目标(1.0 = 正常)
static volatile float g_fmodApplied = 1.0f;   // 游戏线程实际已经设成多少
static volatile LONG  g_fmodApplyN = 0;
static volatile unsigned long long g_fmodArg[4];
static void fmod_on_update(void* sys);

// 真正干活: 只能从游戏线程(fmod_on_update 里)调用
static void fmod_apply_now(void* sys, float ratio)
{
    typedef int (WINAPI *GetMaster_t)(void*, void**);
    static GetMaster_t getMaster = NULL;
    void* mg = NULL;
    int i, n = 0;
    if (sys == NULL) return;
    fmod_load_api();
    if (getMaster == NULL)
    {
        HMODULE h = GetModuleHandleA("fmodex64.dll");
        if (h == NULL) h = LoadLibraryA("fmodex64.dll");
        if (h == NULL) return;
        getMaster = (GetMaster_t)GetProcAddress(h, "FMOD_System_GetMasterChannelGroup");
    }
    if (getMaster == NULL) return;
    if (f_getSwFmt)
    {
        int rate = 0;
        if (f_getSwFmt(sys, &rate, NULL, NULL) == 0 && rate > 8000) g_fmodOutRate = (float)rate;
    }
    if (getMaster(sys, &mg) != 0 || mg == NULL) return;
    g_fchDiag[0] = g_fchDiag[1] = g_fchDiag[2] = 0;
    g_fchDiag[4] = -1;

    if (ratio >= 0.995f)                                  // 还原
    {
        if (f_ovrFreq) g_fchDiag[4] = f_ovrFreq(mg, g_fmodOutRate);
        for (i = 0; i < g_fchN; i++)
        {
            int pl = 0;
            if (f_isPlaying && f_isPlaying(g_fch[i], &pl) != 0) continue;
            if (f_setFreq) f_setFreq(g_fch[i], g_fchFreq[i]);
        }
        g_fchN = 0;
        g_fchSlowN = 0;
        g_fmodApplied = 1.0f;
        InterlockedIncrement(&g_fmodApplyN);
        return;
    }
    // ① 全局覆盖频率(探针证明有效, 音乐也在内)
    if (f_ovrFreq) g_fchDiag[4] = f_ovrFreq(mg, g_fmodOutRate * ratio);
    // ★v115: 只做这一条 —— 上一次崩是因为又去逐个声道枚举(1024 次 GetChannel)+ 递归子组。
    //   现在每个窗口只调 1~2 次 OverrideFrequency, 风险最小的做法。
    g_fchSlowN = 0;
    g_fmodApplied = ratio;
    InterlockedIncrement(&g_fmodApplyN);
    return;
    // ② master group 里逐个在播声道(不再碰子组)
    g_fchN = 0;
    if (f_getNumCh && f_getCh && f_getFreq && f_setFreq)
    {
        if (f_getNumCh(mg, &n) != 0) n = 0;
        if (n > 256) n = 256;
        for (i = 0; i < n && g_fchN < FMOD_MAXCH; i++)
        {
            void* ch = NULL;
            float f = 0.0f;
            if (f_getCh(mg, i, &ch) != 0 || ch == NULL) continue;
            InterlockedIncrement(&g_fchDiag[0]);
            if (f_getFreq(ch, &f) != 0 || f < 50.0f || f > 200000.0f) continue;
            InterlockedIncrement(&g_fchDiag[1]);
            if (f_setFreq(ch, f * ratio) == 0)
            {
                g_fch[g_fchN] = ch; g_fchFreq[g_fchN] = f; g_fchN++;
                InterlockedIncrement(&g_fchDiag[2]);
            }
        }
    }
    g_fchSlowN = g_fchN;
    g_fmodApplied = ratio;
    InterlockedIncrement(&g_fmodApplyN);
}

// 在游戏线程(System::update 的钩子里)调用
static void fmod_on_update(void* sys)
{
    if (sys == NULL) return;
    if (g_fmodSys == 0) g_fmodSys = (unsigned long long)(uintptr_t)sys;
    if (!g_fmodOn) return;
    if (g_fmodWant != g_fmodApplied)
    {
        fmod_apply_now(sys, g_fmodWant);
        if (g_slowF)
        {
            fprintf(g_slowF, "t=%u FMOD 生效 %.2f (声道改动=%ld 枚举=%ld 读频=%ld 写频=%ld ovr=%ld 采样率=%.0f)\n",
                    (unsigned)GetTickCount(), (double)g_fmodWant,
                    (long)g_fchSlowN, (long)g_fchDiag[0], (long)g_fchDiag[1],
                    (long)g_fchDiag[2], (long)g_fchDiag[4], (double)g_fmodOutRate);
            fflush(g_slowF);
        }
    }
}

// 给 FMOD::System::update 打补丁: 保存参数 -> 调我们的 C 函数(游戏线程) -> 恢复参数 -> 原样跑
static bool fmod_update_hook(void)
{
    static const unsigned char upd[9] = { 0x48,0x83,0xEC,0x28, 0x48,0x8D,0x54,0x24,0x38 };
    HMODULE h = GetModuleHandleA("fmodex64.dll");
    unsigned char* fp;
    unsigned char* stub;
    unsigned char orig[9];
    DWORD oldp = 0;
    long rel;
    int i;
    if (h == NULL) h = LoadLibraryA("fmodex64.dll");
    if (h == NULL) return false;
    fp = (unsigned char*)GetProcAddress(h, "?update@System@FMOD@@QEAA?AW4FMOD_RESULT@@XZ");
    if (fp == NULL) return false;
    if (!safe_read((unsigned long long)(uintptr_t)fp, orig, 9)) return false;
    if (memcmp(orig, upd, 9) != 0) return false;
    stub = (unsigned char*)fx_alloc_near(fp, 256);
    if (stub == NULL) return false;
    {
        unsigned char* d = stub;
#define EMIT_RAX_ADDR(a) do { d[0]=0x48; d[1]=0xB8; *(unsigned long long*)(d+2)=(unsigned long long)(uintptr_t)(a); d+=10; } while(0)
        // ★v115: 参数只用**栈**保存(以前存在共享全局里 —— 如果游戏多线程调 update,
        //   参数会互相踩, 跳回去就崩。这就是之前两次卡退的根因)
        d[0]=0x48; d[1]=0x83; d[2]=0xEC; d[3]=0x48; d+=4;                          // sub rsp,0x48
        d[0]=0x48; d[1]=0x89; d[2]=0x4C; d[3]=0x24; d[4]=0x20; d+=5;               // mov [rsp+20],rcx
        d[0]=0x48; d[1]=0x89; d[2]=0x54; d[3]=0x24; d[4]=0x28; d+=5;               // mov [rsp+28],rdx
        d[0]=0x4C; d[1]=0x89; d[2]=0x44; d[3]=0x24; d[4]=0x30; d+=5;               // mov [rsp+30],r8
        d[0]=0x4C; d[1]=0x89; d[2]=0x4C; d[3]=0x24; d[4]=0x38; d+=5;               // mov [rsp+38],r9
        EMIT_RAX_ADDR(&fmod_on_update);d[0]=0xFF; d[1]=0xD0; d+=2;                 // call rax (rcx 还是 this)
        d[0]=0x48; d[1]=0x8B; d[2]=0x4C; d[3]=0x24; d[4]=0x20; d+=5;               // mov rcx,[rsp+20]
        d[0]=0x48; d[1]=0x8B; d[2]=0x54; d[3]=0x24; d[4]=0x28; d+=5;               // mov rdx,[rsp+28]
        d[0]=0x4C; d[1]=0x8B; d[2]=0x44; d[3]=0x24; d[4]=0x30; d+=5;               // mov r8,[rsp+30]
        d[0]=0x4C; d[1]=0x8B; d[2]=0x4C; d[3]=0x24; d[4]=0x38; d+=5;               // mov r9,[rsp+38]
        d[0]=0x48; d[1]=0x83; d[2]=0xC4; d[3]=0x48; d+=4;                          // add rsp,0x48
        memcpy(d, orig, 9); d += 9;                                                // 重放被覆盖的指令
        EMIT_RAX_ADDR(fp + 9);         d[0]=0xFF; d[1]=0xE0; d+=2;                 // jmp rax
#undef EMIT_RAX_ADDR
        FlushInstructionCache(GetCurrentProcess(), stub, (SIZE_T)(d - stub));
    }
    rel = (long)(stub - (fp + 5));
    if (!VirtualProtect(fp, 9, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    suspend_others(true);
    fp[0] = 0xE9;
    *(long*)(fp + 1) = rel;
    for (i = 5; i < 9; i++) fp[i] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), fp, 9);
    suspend_others(false);
    VirtualProtect(fp, 9, oldp, &oldp);
    return true;
}

// 缓速时把游戏音频的 pitch 拉下来; 结束时还原 1.0
//  ★v101: 日志证明 master channel group 的 SetPitch 调成功了(次数=224)但**听不出来** ——
//   FMOD Ex 对 master group 的 pitch 基本是空操作。所以改成"逐个在播声道改频率":
//   音乐、音效都在里面, 效果非常明显。还原时把记下来的原频率写回去。
#if 0
static void*  g_fch[FMOD_MAXCH];
static float  g_fchFreq[FMOD_MAXCH];
static int    g_fchN = 0;
static volatile LONG g_fchSlowN = 0;      // 最近一次降速改了几个声道(日志用)
// ★v102: v101 的日志显示"这次改了 0 个声道" —— System_GetChannel 一个都没拿到。
//   这版改成三条路一起上, 并且把过程中的计数写进日志, 下一次就能看出卡在哪一步:
//     ① master channel group 的 OverrideFrequency(绝对频率, 最可能"一调就是全局")
//     ② 递归遍历 channel group 里的声道(组 → 子组)逐个 SetFrequency
//     ③ 再兜底扫一遍 System_GetChannel 的 id
static volatile LONG g_fchDiag[8];   // 0=枚举到的声道 1=读到频率 2=写入成功 3=组数
                                     // 4=OverrideFrequency 返回值 5=System_GetChannel 拿到
static volatile float g_fmodOutRate = 44100.0f;

typedef int (WINAPI *F_GetNumCh_t)(void*, int*);
typedef int (WINAPI *F_GetCh_t)(void*, int, void**);
typedef int (WINAPI *F_GetFreq_t)(void*, float*);
typedef int (WINAPI *F_SetFreq_t)(void*, float);
typedef int (WINAPI *F_GetNumG_t)(void*, int*);
typedef int (WINAPI *F_GetG_t)(void*, int, void**);
typedef int (WINAPI *F_OverrideFreq_t)(void*, float);
typedef int (WINAPI *F_IsPlaying_t)(void*, int*);
typedef int (WINAPI *F_GetSoftwareFormat_t)(void*, int*, void*, int*);

static F_GetNumCh_t f_getNumCh = NULL;
static F_GetCh_t    f_getCh = NULL;
static F_GetFreq_t  f_getFreq = NULL;
static F_SetFreq_t  f_setFreq = NULL;
static F_GetNumG_t  f_getNumG = NULL;
static F_GetG_t     f_getG = NULL;
static F_OverrideFreq_t f_ovrFreq = NULL;
static F_IsPlaying_t f_isPlaying = NULL;
static void*        f_sysGetChannel = NULL;   // FMOD_System_GetChannel
static void*        f_sysGetChansPlaying = NULL;
#endif

static void fmod_load_api(void)
{
    HMODULE h;
    if (f_getCh != NULL) return;
    h = GetModuleHandleA("fmodex64.dll");
    if (h == NULL) h = LoadLibraryA("fmodex64.dll");
    if (h == NULL) return;
    f_getNumCh    = (F_GetNumCh_t)   GetProcAddress(h, "FMOD_ChannelGroup_GetNumChannels");
    f_getCh       = (F_GetCh_t)      GetProcAddress(h, "FMOD_ChannelGroup_GetChannel");
    f_getFreq     = (F_GetFreq_t)    GetProcAddress(h, "FMOD_Channel_GetFrequency");
    f_setFreq     = (F_SetFreq_t)    GetProcAddress(h, "FMOD_Channel_SetFrequency");
    f_getNumG     = (F_GetNumG_t)    GetProcAddress(h, "FMOD_ChannelGroup_GetNumGroups");
    f_getG        = (F_GetG_t)       GetProcAddress(h, "FMOD_ChannelGroup_GetGroup");
    f_ovrFreq     = (F_OverrideFreq_t)GetProcAddress(h, "FMOD_ChannelGroup_OverrideFrequency");
    f_isPlaying   = (F_IsPlaying_t)  GetProcAddress(h, "FMOD_Channel_IsPlaying");
    f_sysGetChannel = (void*)        GetProcAddress(h, "FMOD_System_GetChannel");
    f_sysGetChansPlaying = (void*)   GetProcAddress(h, "FMOD_System_GetChannelsPlaying");
    f_getSwFmt    = (F_GetSoftwareFormat_t)GetProcAddress(h, "FMOD_System_GetSoftwareFormat");
    if (f_getSwFmt && g_fmodSys)
    {
        int rate = 0;
        if (f_getSwFmt((void*)(uintptr_t)g_fmodSys, &rate, NULL, NULL) == 0 && rate > 8000)
            g_fmodOutRate = (float)rate;
    }
}

// 递归: 把组里的声道频率都乘 ratio
static void fmod_take_channels(void* grp, float ratio, int depth)
{
    int n = 0, i;
    if (grp == NULL || depth > 3) return;
    if (f_getNumCh && f_getNumCh(grp, &n) == 0 && n > 0)
    {
        if (n > 512) n = 512;
        for (i = 0; i < n; i++)
        {
            void* ch = NULL;
            float f = 0.0f;
            if (f_getCh == NULL || f_getCh(grp, i, &ch) != 0 || ch == NULL) continue;
            InterlockedIncrement(&g_fchDiag[0]);
            if (f_getFreq(ch, &f) != 0 || f < 50.0f || f > 200000.0f) continue;
            InterlockedIncrement(&g_fchDiag[1]);
            if (f_setFreq(ch, f * ratio) == 0 && g_fchN < FMOD_MAXCH)
            {
                g_fch[g_fchN] = ch;
                g_fchFreq[g_fchN] = f;
                g_fchN++;
                InterlockedIncrement(&g_fchDiag[2]);
            }
        }
    }
    if (f_getNumG && f_getG)
    {
        int ng = 0;
        if (f_getNumG(grp, &ng) == 0 && ng > 0)
        {
            if (ng > 32) ng = 32;
            for (i = 0; i < ng; i++)
            {
                void* sub = NULL;
                InterlockedIncrement(&g_fchDiag[3]);
                if (f_getG(grp, i, &sub) == 0 && sub != NULL)
                    fmod_take_channels(sub, ratio, depth + 1);
            }
        }
    }
}

static void fmod_slow_channels(float ratio)
{
    typedef int (WINAPI *GetMaster_t)(void*, void**);
    static GetMaster_t getMaster = NULL;
    void* mg = NULL;
    int i;
    if (g_fmodSys == 0) return;
    fmod_load_api();
    if (getMaster == NULL)
    {
        HMODULE h = GetModuleHandleA("fmodex64.dll");
        if (h == NULL) h = LoadLibraryA("fmodex64.dll");
        if (h == NULL) return;
        getMaster = (GetMaster_t)GetProcAddress(h, "FMOD_System_GetMasterChannelGroup");
    }
    if (getMaster == NULL) return;
    if (getMaster((void*)(uintptr_t)g_fmodSys, &mg) != 0 || mg == NULL) return;

    g_fchDiag[0] = g_fchDiag[1] = g_fchDiag[2] = g_fchDiag[3] = 0;
    g_fchDiag[4] = -1; g_fchDiag[5] = 0;

    if (ratio >= 0.995f)                      // 还原
    {
        if (f_ovrFreq) f_ovrFreq(mg, g_fmodOutRate);
        for (i = 0; i < g_fchN; i++)
        {
            int pl = 0;
            if (f_isPlaying && f_isPlaying(g_fch[i], &pl) != 0) continue;
            if (f_setFreq) f_setFreq(g_fch[i], g_fchFreq[i]);
        }
        g_fchN = 0;
        g_fchSlowN = 0;
        return;
    }
    // ① 全局覆盖频率(最直接的"音乐慢下来")
    if (f_ovrFreq) g_fchDiag[4] = f_ovrFreq(mg, g_fmodOutRate * ratio);
    // ② 组里逐个声道(含子组)
    g_fchN = 0;
    fmod_take_channels(mg, ratio, 0);
    // ③ 兜底: System_GetChannel 扫 id
    if (f_sysGetChannel && f_getFreq && f_setFreq)
    {
        typedef int (WINAPI *GCh_t)(void*, int, void**);
        GCh_t gch = (GCh_t)f_sysGetChannel;
        for (i = 0; i < 1024 && g_fchN < FMOD_MAXCH; i++)
        {
            void* ch = NULL;
            float f = 0.0f;
            if (gch((void*)(uintptr_t)g_fmodSys, i, &ch) != 0 || ch == NULL) continue;
            InterlockedIncrement(&g_fchDiag[5]);
            if (f_getFreq(ch, &f) != 0 || f < 50.0f) continue;
            if (f_setFreq(ch, f * ratio) == 0)
            {
                g_fch[g_fchN] = ch; g_fchFreq[g_fchN] = f; g_fchN++;
                InterlockedIncrement(&g_fchDiag[2]);
            }
        }
    }
    g_fchSlowN = g_fchN;
}

static void fmod_apply_pitch(float p)
{
    typedef int (WINAPI *GetMaster_t)(void*, void**);
    typedef int (WINAPI *SetPitch_t)(void*, float);
    static GetMaster_t getMaster = NULL;
    static SetPitch_t  setPitch  = NULL;
    void* mg = NULL;
    if (!g_fmodOn || g_fmodSys == 0) return;
    if (getMaster == NULL || setPitch == NULL)
    {
        HMODULE h = GetModuleHandleA("fmodex64.dll");
        if (h == NULL) h = LoadLibraryA("fmodex64.dll");
        if (h == NULL) return;
        if (getMaster == NULL) getMaster = (GetMaster_t)GetProcAddress(h, "FMOD_System_GetMasterChannelGroup");
        if (setPitch  == NULL) setPitch  = (SetPitch_t) GetProcAddress(h, "FMOD_ChannelGroup_SetPitch");
        if (getMaster == NULL || setPitch == NULL) return;
    }
    if (getMaster((void*)(uintptr_t)g_fmodSys, &mg) == 0 && mg != NULL)
    {
        if (setPitch(mg, p) == 0)
        {
            g_fmodPitchNow = p;
            InterlockedIncrement(&g_fmodSets);
        }
    }
    // ★v101: 真正起作用的是这一条 —— 逐个声道改频率(音乐+音效)
    fmod_slow_channels(p);
}
static int fmodWasSlow = 0;

// ★v100: "玩家是不是在空中"(被义父踩飞 / 跳起来)。
//   判据: 120ms 内玩家高度变化 >= 0.3 米(上升或下坠都算)。
//   空中本来就按不出垫步, 所以这时候按下闪避触发的缓速一律作废。
static volatile LONG g_playerAir = 0;
static void air_update(DWORD now)
{
    static DWORD lastT = 0;
    static float lastY = 0.0f;
    unsigned long long pc = ctr_of_player();
    float p[3];
    if (pc == 0 || !ctr_pos(pc, p)) { g_playerAir = 0; return; }
    if (lastT == 0 || (now - lastT) >= 120)
    {
        if (lastT != 0 && (now - lastT) <= 400)
        {
            float dy = p[1] - lastY;
            if (dy < 0.0f) dy = -dy;
            g_playerAir = (dy >= 0.30f) ? 1 : 0;
        }
        lastY = p[1]; lastT = now;
    }
}
#define FX_LOG(...) do { if (g_slowF) { fprintf(g_slowF, __VA_ARGS__); fflush(g_slowF); } } while (0)
#define FX_ACTIVE() ((fx_on && (g_slowActive || fx_fade > 0.01f)) ? 1 : 0)
#define FX_TICK() fx_tick()
#include "fx_glow.h"

// 缓速窗口的淡入(90ms) / 淡出(最后 220ms + 尾巴)
static void fx_tick(void)
{
    DWORD now = GetTickCount();
    if (g_slowActive)
    {
        DWORD el = now - g_slowStart;
        // ★v100: 出现过程压到 120ms(用户说"一直变"晃眼 -> 让它快点定住)
        float f  = (el < 120) ? ((float)el / 120.0f) : 1.0f;
        DWORD left = (el < g_slowDur) ? (g_slowDur - el) : 0;
        float g = (float)left / 150.0f;
        if (g < f) f = g;
        if (f > 1.0f) f = 1.0f;
        if (f < 0.0f) f = 0.0f;
        fx_fade = f;
    }
    else if (g_slowOffAt != 0 && (DWORD)(now - g_slowOffAt) < 420)
    {
        // ★v101: 用户"消失别一瞬间" —— 420ms 里从结束那一刻的强度平滑归零
        float k = 1.0f - (float)(now - g_slowOffAt) / 420.0f;
        if (k < 0.0f) k = 0.0f;
        fx_fade = g_fadeFrom * k;
    }
    else fx_fade = 0.0f;
}

// ★v98: 成功闪避那一瞬间的"时间拉长"提示音。
//   说明: 这是**我们自己的音效**, 不是把游戏原本的音效拖长 ——
//   真要把游戏音频拖长, 得另挂 XAudio2 的 voice 频率比(下一个独立步骤)。
//   换音效: 直接替换那个 wav 文件就行; MODE 里写 nosfx 可以关掉。
// ★v126: 缓速结束 -> 立刻掐掉提示音(MCI stop), 让声音只活在缓速窗口里
static void fx_sting_stop(void)
{
    typedef DWORD (WINAPI *MCI_t)(LPCSTR, LPSTR, UINT, HWND);
    static MCI_t mciS = NULL;
    static LONG triedS = 0;
    if (InterlockedCompareExchange(&triedS, 1, 0) == 0)
    {
        HMODULE h = LoadLibraryA("winmm.dll");
        if (h) mciS = (MCI_t)GetProcAddress(h, "mciSendStringA");
    }
    // ★v127: 用户要"平滑淡出" —— 先记下淡出窗口, 由主循环每帧把音量台阶式降到 0 再停。
    g_stingFadeAt = GetTickCount();
    g_stingFading = 1;
}

static void fx_sting(void)
{
    typedef BOOL (WINAPI *PS_t)(LPCSTR, HMODULE, DWORD);
    static PS_t ps = NULL;
    static LONG tried = 0;
    if (!g_sfxOn) return;          // ★v99: 默认关(用户说那个提示音像 bb 枪)
    if (InterlockedCompareExchange(&tried, 1, 0) == 0)
    {
        HMODULE h = LoadLibraryA("winmm.dll");
        if (h) ps = (PS_t)GetProcAddress(h, "PlaySoundA");
    }
    if (ps == NULL) return;
    // ★v125: 改成能直接播 mp3(MCI, 只要 winmm 就能播, 不需要任何解码器) ——
    //   这样用户给的参考音(刀划破空气那种)可以直接丢进 assets 用。
    {
        typedef DWORD (WINAPI *MCI_t)(LPCSTR, LPSTR, UINT, HWND);
        static MCI_t mci = NULL;
        static LONG tried2 = 0;
        if (InterlockedCompareExchange(&tried2, 1, 0) == 0)
        {
            HMODULE h2 = LoadLibraryA("winmm.dll");
            if (h2) mci = (MCI_t)GetProcAddress(h2, "mciSendStringA");
        }
        // ★v128: 不再用 MCI/mp3 —— 很多机器没装 MCI 的 mpegvideo 设备, open 失败就没声音。
        //   改成直接播 WAV(PlaySound, 最稳的一条路), 内容已经是加工好的(降调/拉长/低通/淡出)。
        if (0 && mci != NULL)
        {
            mci("close fxwarp", NULL, 0, NULL);
            mci("open \"C:\\Users\\47297\\Documents\\Codex\\2026-09-15\\ru-g\\sekiro-mod\\assets\\fx_warp.mp3\" type mpegvideo alias fxwarp", NULL, 0, NULL);
            mci("play fxwarp", NULL, 0, NULL);
            return;
        }
    }
    ps(P_WAV(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

// ---- ?????"??????"?? ----
// ??: ????????????, ???"????ID"???????????
// ????????????, ???????, ??"????"??????
// ????(3002/3003/3004 ??)?????, ??????
// ??: ???????????, 3000/3001 ??????????
// ???"????"????; ???????????(????????)?
//   key 0 = ??
//   key>0 = ??????? CharacterId / NpcId
#define ASET_MAX  24     // ??????????
#define ATYPE_MAX 10     // ????????
typedef struct { int key; int n; int ids[ASET_MAX]; } AtkSet;
static AtkSet        g_at[ATYPE_MAX];
static volatile LONG g_atN = 0;

// ChrIns -> ?? key ???(?"??"?????)
#define CMAP_MAX 16
static unsigned long long g_cmChr[CMAP_MAX];
static int                g_cmKey[CMAP_MAX];
static volatile LONG      g_cmN = 0;

static AtkSet* at_get(int key, int create)
{
    LONG n = g_atN;
    int i;
    for (i = 0; i < n && i < ATYPE_MAX; i++) if (g_at[i].key == key) return &g_at[i];
    if (!create || n >= ATYPE_MAX) return NULL;
    g_at[n].key = key;
    g_at[n].n = 0;
    InterlockedIncrement(&g_atN);
    return &g_at[n];
}
static AtkSet* at_global(void) { return at_get(0, 1); }

static int at_has(AtkSet* s, int id)
{
    int i;
    if (s == NULL || id < 0) return 0;
    for (i = 0; i < s->n && i < ASET_MAX; i++) if (s->ids[i] == id) return 1;
    return 0;
}

// ??: ?????????(model)?, ?"??????????????"??
static int anim_is_learned_attack(int anim)
{
    AtkSet* gs = at_get(0, 0);
    if (gs == NULL || anim < 0) return 0;
    return at_has(gs, anim);
}
static void at_add(AtkSet* s, int id)
{
    int i;
    if (s == NULL || id < 0) return;
    for (i = 0; i < s->n && i < ASET_MAX; i++) if (s->ids[i] == id) return;
    if (s->n >= ASET_MAX) return;
    s->ids[s->n++] = id;
    if (g_slowF)
    {
        fprintf(g_slowF, "t=%u LEARN attack-anim id=%d  type=%d (typeN=%d globalN=%d)\n",
                (unsigned)GetTickCount(), id, s->key, s->n,
                at_get(0, 1) ? at_get(0, 1)->n : 0);
        fflush(g_slowF);
    }
}
static void cmap_put(unsigned long long chr, int key)
{
    LONG n = g_cmN;
    int i;
    if (chr == 0 || key == 0) return;
    for (i = 0; i < n && i < CMAP_MAX; i++) if (g_cmChr[i] == chr) { g_cmKey[i] = key; return; }
    if (n < CMAP_MAX) { g_cmChr[n] = chr; g_cmKey[n] = key; InterlockedIncrement(&g_cmN); }
}
static int cmap_get(unsigned long long chr)
{
    LONG n = g_cmN;
    int i;
    if (chr == 0) return 0;
    for (i = 0; i < n && i < CMAP_MAX; i++) if (g_cmChr[i] == chr) return g_cmKey[i];
    return 0;
}

// ?"??? Man"????????CT ?: CharacterId=[Man+0x68],
// NpcId=[[Man+0x30]+0x628], ThinkId=[[Man+0x30]+0x624]
static int chr_type(unsigned long long man, int* cidOut, int* npcOut, int* thOut)
{
    int cid = -1, npc = -1, th = -1;
    unsigned long long a = 0;
    if (cidOut) *cidOut = cid;
    if (npcOut) *npcOut = npc;
    if (thOut)  *thOut = th;
    if (man == 0) return 0;
    rd_u32(man + 0x68, &cid);
    if (rd_ptr(man + 0x30, &a) && a != 0)
    {
        rd_u32(a + 0x628, &npc);
        rd_u32(a + 0x624, &th);
    }
    if (cidOut) *cidOut = cid;
    if (npcOut) *npcOut = npc;
    if (thOut)  *thOut = th;
    if (cid > 0 && cid < 100000) return cid;
    if (npc > 0 && npc < 100000) return npc;
    if (th  > 0 && th  < 100000) return th;
    return 0;
}

static void fp_str(unsigned long long addr, char* out, int n)
{
    int i;
    out[0] = 0;
    if (addr == 0 || n < 2) return;
    if (!safe_read(addr, out, (size_t)(n - 1))) { out[0] = 0; return; }
    out[n - 1] = 0;
    for (i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)out[i];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) out[i] = '.';
    }
    out[i] = 0;
}

static void fp_hex(FILE* o, const char* label, unsigned long long addr, int n)
{
    static unsigned char buf[0x400];
    int i;
    if (o == NULL) return;
    if (addr == 0 || n <= 0 || n > (int)sizeof(buf)) { fprintf(o, "%s=?\n", label); return; }
    if (!safe_read(addr, buf, (size_t)n)) { fprintf(o, "%s=?\n", label); return; }
    fprintf(o, "%s=", label);
    for (i = 0; i < n; i++) fprintf(o, "%02X", buf[i]);
    fputc('\n', o);
}

// ????"??????"????? HIT / DODGE / QUIET ??????
// ????????"??????"?
// ---- ?? ChrIns ? 0x300 ????? (100ms x 16 = ?? 1.6 ?) ----
// ???????? 1.6 ????: ???"??"?????????
#define HRING 16
static unsigned char      g_hist[HRING][0x300];
static unsigned long long g_histChr[HRING];
static DWORD              g_histT[HRING];
static int                g_histN = 0;
// ????????"????"(container+0x10) ?? ?????????????
static unsigned char      g_histA[HRING][0x100];
static unsigned long long g_histAO[HRING];

static void fp_hex_mem(FILE* o, const char* label, const unsigned char* buf, int n)
{
    int i;
    if (o == NULL) return;
    fprintf(o, "%s=", label);
    for (i = 0; i < n; i++) fprintf(o, "%02X", buf[i]);
    fputc('\n', o);
}

static void set_player_playspeed(float v)
{
    unsigned long long pc = ctr_of_player(), ao = 0;
    if (pc != 0 && rd_ptr(pc + 0x28, &ao) && ao != 0) safe_write(ao + 0xD00, &v, 4);
}

// ★v94: 读回玩家自己的动画播放速度(和 chrwatch 打印的 pspeed 是同一个地址)
static bool get_player_playspeed(float* out)
{
    unsigned long long pc = ctr_of_player(), ao = 0;
    if (pc == 0 || !rd_ptr(pc + 0x28, &ao) || ao == 0) return false;
    return safe_read(ao + 0xD00, out, 4);
}

// ---------- 0xB69000 ???: ???"???? + ????" ----------
// 0xB69000 ????????"????"??(0xBE00D0/0xBE0170)?
// ???????????(0xB6F690)????
// ???????/???????? .text dump ? -> ???**??????**???
// (vtable ? .rdata, ?????? dump ?)????????????????????????
//
// ???? prologue 15 ??, ????????, ??????????:
//   48 89 5C 24 08   mov [rsp+8],rbx
//   48 89 6C 24 10   mov [rsp+0x10],rbp
//   48 89 74 24 18   mov [rsp+0x18],rsi
//   57               push rdi
static volatile unsigned long long g_arA1 = 0, g_arA2 = 0;
static volatile unsigned g_arA3 = 0, g_arA4 = 0;
static volatile unsigned long long g_arRet = 0;
static volatile LONG g_arSeq = 0, g_arHookOk = 0;
static volatile LONG g_arHitSnap = 0;   // ????????????(????)
static volatile unsigned long long g_dbgA1ch = 0, g_dbgA2ch = 0, g_dbgPch = 0;

static bool install_attackres_hook(void)
{
    // ? ??????? 0xB68FF0(0xB68FEF ? CC ??), ?? 0xB69000?
    //   0xB69000 ???????(??????push rdi ??), v54 ?????? 16 ???????
    unsigned long long tgt = g_base + 0xB68FF0ULL;
    static const unsigned char expect[16] = {
        0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18, 0x57
    };
    unsigned char orig[16];
    unsigned char* p;
    unsigned char* dst;
    DWORD oldp = 0;
    if (!safe_read(tgt, orig, 16) || memcmp(orig, expect, 16) != 0) return false;
    p = (unsigned char*)VirtualAlloc(NULL, 0x200, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (p == NULL) return false;
    dst = p;
#define EMIT_RAX(a) do { dst[0]=0x48; dst[1]=0xB8; \
        *(unsigned long long*)(dst+2)=(unsigned long long)(uintptr_t)(a); dst += 10; } while (0)
    EMIT_RAX(&g_arA1); dst[0]=0x48; dst[1]=0x89; dst[2]=0x08; dst += 3;   // mov [rax],rcx
    EMIT_RAX(&g_arA2); dst[0]=0x48; dst[1]=0x89; dst[2]=0x10; dst += 3;   // mov [rax],rdx
    EMIT_RAX(&g_arA3); dst[0]=0x44; dst[1]=0x89; dst[2]=0x00; dst += 3;   // mov [rax],r8d
    EMIT_RAX(&g_arA4); dst[0]=0x44; dst[1]=0x89; dst[2]=0x08; dst += 3;   // mov [rax],r9d
    dst[0]=0x4C; dst[1]=0x8B; dst[2]=0x1C; dst[3]=0x24; dst += 4;         // mov r11,[rsp]
    EMIT_RAX(&g_arRet); dst[0]=0x4C; dst[1]=0x89; dst[2]=0x18; dst += 3;  // mov [rax],r11
    // ? ?"???????"??? g_hitSeq ?? ? resolve ???????:
    //   ????????**??????**, ?????????
    //   ???????????, ??????"????"?? -> ???????"???"?
    EMIT_RAX(&g_hitSeq);       dst[0]=0x44; dst[1]=0x8B; dst[2]=0x18; dst += 3;  // mov r11d,[rax]
    EMIT_RAX(&g_arHitSnap);    dst[0]=0x44; dst[1]=0x89; dst[2]=0x18; dst += 3;  // mov [rax],r11d
    EMIT_RAX(&g_arSeq); dst[0]=0xF0; dst[1]=0xFF; dst[2]=0x00; dst += 3;  // lock inc dword [rax]
#undef EMIT_RAX
    memcpy(dst, orig, 16); dst += 16;
    dst[0] = 0xFF; dst[1] = 0x25; *(unsigned int*)(dst + 2) = 0;
    *(unsigned long long*)(dst + 6) = tgt + 16; dst += 14;
    FlushInstructionCache(GetCurrentProcess(), p, (SIZE_T)(dst - p));
    {
        unsigned char patch[14];
        patch[0] = 0xFF; patch[1] = 0x25; *(unsigned int*)(patch + 2) = 0;
        *(unsigned long long*)(patch + 6) = (unsigned long long)(uintptr_t)p;
        if (!VirtualProtect((LPVOID)(uintptr_t)tgt, 16, PAGE_EXECUTE_READWRITE, &oldp)) return false;
        suspend_others(true);
        memcpy((void*)(uintptr_t)tgt, patch, 14);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)tgt, 16);
        suspend_others(false);
        VirtualProtect((LPVOID)(uintptr_t)tgt, 15, oldp, &oldp);
    }
    g_arHookOk = 1;
    return true;
}

// ---------- ?????: ?"?????"???? ----------
// ??: ??"????????"?"???????"????, ???????????
// ????, ?????????
static void fp_dump_player(const char* tag, DWORD now, unsigned long long extra)
{
    FILE* o = g_slowF;
    unsigned long long pc = 0, ch = 0, ao = 0;
    int aid = -1, act = -1, hp = -1;
    float len = 0.0f;
    if (o == NULL) return;
    pc = ctr_of_player();
    if (pc != 0)
    {
        aid = anim_of_ctr(pc, &len);
        rd_ptr(pc + 0x18, &ch);
        rd_ptr(pc + 0x10, &ao);
        if (ch != 0) { rd_u32(ch + 0x130, &hp); safe_read(ch + 0x230, &act, 4); }
    }
    fprintf(o, "== PP %s t=%u extra=%u anim=%d elen=%.3f act=%d hp=%d ctr=%llX\n",
            tag, (unsigned)now, (unsigned)extra, aid, len, act, hp, pc);
    fflush(o);
    // ??????
    {
        static unsigned char b[0x400];
        if (ch != 0 && safe_read(ch, b, 0x300)) fp_hex_mem(o, "   C", b, 0x300);
        if (ao != 0 && safe_read(ao, b, 0x100)) fp_hex_mem(o, "   A", b, 0x100);
        if (pc != 0 && safe_read(pc, b, 0x100)) fp_hex_mem(o, "   K", b, 0x100);
    }
    fflush(o);
}

static void fp_dump(const char* tag, DWORD now, float dist, unsigned long long ctr, unsigned long long man)
{
    FILE* o = g_slowF;
    unsigned long long ch = 0, animObj = 0, animObj2 = 0, ai = 0;
    float eLen = 0.0f, pspeed = -1.0f;
    int   eAnim = -1, eAct = -1, eHp = -1, idle = -1;
    char  nm[48];
    if (o == NULL) return;
    fprintf(o, "== FP %s t=%u dist=%.2f ctr=%llX man=%llX\n",
            tag, (unsigned)now, dist, ctr, man);
    if (ctr == 0) { fflush(o); return; }
    rd_ptr(ctr + 0x18, &ch);
    eAnim = anim_of_ctr(ctr, &eLen);
    if (ch != 0) { rd_u32(ch + 0x130, &eHp); safe_read(ch + 0x230, &eAct, 4); }
    rd_ptr(ctr + 0x10, &animObj);
    rd_ptr(ctr + 0x28, &animObj2);
    rd_ptr(ctr + 0x58, &ai);
    if (animObj2 != 0)
    {
        safe_read(animObj2 + 0xD00, &pspeed, 4);
        fp_str(animObj2 + 0x878, nm, sizeof(nm));
    }
    if (ai != 0) rd_u32(ai + 0x10, &idle);
    fprintf(o, "   SUM anim=%d elen=%.2f ehp=%d eact=%d idle=%d pspeed=%.2f name='%s' ch=%llX animObj=%llX ai=%llX\n",
            eAnim, eLen, eHp, eAct, idle, pspeed, nm, ch, animObj, ai);
    if (ai != 0) fp_hex(o, "   ACTB", ai + 0x340 + 0xB730, 32);
    fp_hex(o, "   A", animObj, 0x100);
    fp_hex(o, "   C", ch, 0x300);
    if (man != 0) fp_hex(o, "   M", man, 0x300);
    fflush(o);
}

// ==================== ?????? (2ms) ====================

// ---------- ????????: ??"????"?? ----------
// ?????(sekiro_text.bin):
//   0xB68D00 ???? = "??????????" ????????????
//   (0xB6F690) ????????:
//       B690C1  call 0x140BE00D0   ; ?? 1 => ??????
//       B690E0  call 0x140BE0170   ; ?? 1 => ??????
//   ???? 1 ?????????, ????????????
//   => ????????????: ???????????????????
//   ?? "?????????? 1" == ???????????? == ?????
// ?????"???"? 4 ??????(??????), ???????????
static unsigned long long g_playerInv = 0;   // [[[WorldChrMan+0x88]+0x1FF8]+0x88]
static unsigned long long g_playerMan = 0;   // [WorldChrMan+0x88]
static volatile LONG g_negPlayerN = 0;       // ??????????
static volatile LONG g_negAnyN    = 0;       // ???????
static volatile LONG g_negHookOk  = 0;
static volatile LONG g_invForceUntil = 0;    // v91: 到这个时刻为止, 强制"无敌"(被吃掉)
static volatile LONG g_invForcedN = 0;       // 被强制吃掉的攻击次数(日志用)

// ★★ v106: 用户判定 —— 触发瞬间的 2.2 秒无敌太超模(连段时躲掉第一段就能站着不动)。
//   新规则: **触发瞬间不给任何无敌**; 只有"缓速正常结束"那一刻给 0.25 秒,
//   用来吃那些垫步本来躲不掉的攻击。钉住无敌帧 / 屏蔽伤害 也一起停掉
//   (它们都挂在 s6HoldUntil 上, 这里返回 0 就等于全关)。
//   想看老行为: MODE 里写 oldif。
static DWORD inv_hold_on_trigger(DWORD now)
{
    if (strstr(g_mode, "oldif") != NULL)
    {
        g_invForceUntil = (LONG)(now + 2200);
        return now + 2200;
    }
    // ★★ v113: 用户规则 —— **缓速期间全程无敌**(否则"缓速了还被砍扣血"很坏手感)。
    //   窗口结束那一刻再补 0.25 秒(在结束分支里设)。不再有 2.2 秒那套。
    g_invForceUntil = (LONG)(now + g_slowDur + 100);
    return 0;
}
static volatile LONG g_negLastMs  = 0;

typedef unsigned char (*PFN_INV)(void*);
static PFN_INV g_realInvA = NULL;   // 0x140BE00D0
static PFN_INV g_realInvB = NULL;   // 0x140BE0170

// ---------- ??"????"???????(???) ----------
static unsigned long long g_invObj[24];
static int                g_invRes[24];
static volatile LONG      g_invN = 0;

static void neg_note(void* o, unsigned char r)
{
    InterlockedIncrement(&g_negAnyN);
    {
        LONG k = InterlockedIncrement(&g_invN) - 1;
        if (k < 24) { g_invObj[k & 23] = (unsigned long long)(uintptr_t)o; g_invRes[k & 23] = r ? 1 : 0; }
    }
    if (r && g_playerInv != 0 && (unsigned long long)(uintptr_t)o == g_playerInv)
    {
        g_negLastMs = (LONG)GetTickCount();
        InterlockedIncrement(&g_negPlayerN);
    }
}

// ★v92: "被做无敌判定的是不是玩家自己" —— 只认玩家, 绝不碰敌人。
//   多认几种形式(ChrIns / 容器 / 玩家无敌对象), 认不出就返回 0(宁可不生效,
//   也不能像 v91 那样把敌人的攻击判定也一起废掉)。
static int is_player_obj(void* o)
{
    unsigned long long v = (unsigned long long)(uintptr_t)o;
    if (v == 0) return 0;
    if (g_playerInv != 0 && v == g_playerInv) return 1;
    {
        unsigned long long pch = player_chr();
        if (pch != 0 && v == pch) return 1;
    }
    {
        unsigned long long pctr = ctr_of_player();
        if (pctr != 0 && v == pctr) return 1;
    }
    return 0;
}

extern "C" unsigned char inv_probe_a(void* o)
{
    // ★★ v91: 在"成功闪避保护窗口"内强制判定成"无敌" —— 这一击直接被游戏吃掉
    //   (不掉血、也不会出现挨打动作)。比"事后把血量写回去"强得多。
    // ★★ v92: 但必须确认"被判定的是玩家自己"! v91 忘了检查, 于是窗口内连敌人
    //   也被判成无敌 -> 玩家寄鹰斩/旋风斩打上去既不格挡也不掉血(用户报的 bug)。
    if (g_invForceUntil != 0 && (LONG)(g_invForceUntil - (LONG)GetTickCount()) > 0 && is_player_obj(o))
    {
        InterlockedIncrement(&g_invForcedN);
        return 1;
    }
    unsigned char r = g_realInvA ? g_realInvA(o) : 0;
    neg_note(o, r);
    return r;
}
extern "C" unsigned char inv_probe_b(void* o)
{
    if (g_invForceUntil != 0 && (LONG)(g_invForceUntil - (LONG)GetTickCount()) > 0 && is_player_obj(o))
    {
        InterlockedIncrement(&g_invForcedN);
        return 1;
    }
    unsigned char r = g_realInvB ? g_realInvB(o) : 0;
    neg_note(o, r);
    return r;
}

// ? near ????????????(?1.5GB ??), ???????????
static void* alloc_near(unsigned long long nearAddr, size_t size)
{
    SYSTEM_INFO si;
    unsigned long long gran, lo, hi, a;
    GetSystemInfo(&si);
    gran = si.dwAllocationGranularity ? (unsigned long long)si.dwAllocationGranularity : 0x10000ULL;
    lo = (nearAddr > 0x60000000ULL) ? (nearAddr - 0x60000000ULL) : 0x10000ULL;
    hi = nearAddr + 0x60000000ULL;
    for (a = nearAddr & ~(gran - 1); a < hi; a += gran)
    {
        void* p = VirtualAlloc((LPVOID)(uintptr_t)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    for (a = nearAddr & ~(gran - 1); a > lo; a -= gran)
    {
        void* p = VirtualAlloc((LPVOID)(uintptr_t)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

// ???: mov rax, <????> ; jmp rax   (12 ??)
// ????? DLL ???? 0x7FF... ??, ?????(0x140000000)? 700+ GB,
// call rel32(?2GB) ???????? 32 ??? ?? v36 ????????
// (WER: ???? unknown, ?? 0x132A41EC0)?
static void* make_abs_stub(unsigned long long nearAddr, void* target, unsigned long long* out)
{
    unsigned char* p = (unsigned char*)alloc_near(nearAddr, 0x1000);
    if (p == NULL) return NULL;
    p[0] = 0x48; p[1] = 0xB8;                                  // mov rax, imm64
    *(unsigned long long*)(p + 2) = (unsigned long long)(uintptr_t)target;
    p[10] = 0xFF; p[11] = 0xE0;                                // jmp rax
    FlushInstructionCache(GetCurrentProcess(), p, 16);
    if (out) *out = (unsigned long long)(uintptr_t)p;
    return p;
}

static bool patch_call_target(unsigned long long callSite, void* fn, unsigned long long* stubOut)
{
    DWORD oldp = 0;
    unsigned long long stub = 0;
    long long rel;
    if (make_abs_stub(callSite, fn, &stub) == NULL) return false;
    rel = (long long)stub - (long long)(callSite + 5);
    if (rel < -0x70000000LL || rel > 0x70000000LL) return false;   // ??????
    if (stubOut) *stubOut = stub;
    if (!VirtualProtect((LPVOID)(uintptr_t)(callSite + 1), 4, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    suspend_others(true);
    *(LONG*)(uintptr_t)(callSite + 1) = (LONG)rel;
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)callSite, 5);
    suspend_others(false);
    VirtualProtect((LPVOID)(uintptr_t)(callSite + 1), 4, oldp, &oldp);
    return true;
}

static unsigned long long g_invStubA = 0, g_invStubB = 0;
static volatile LONG g_invLog = -1;      // -1 ?? / 1 ?? / 0 ??

// ---------- 0xB68D00 ???: "?????????????" ----------
// 0xB68D00(victim, hitInfo, a3, a4) ???????????
// ??????"?? return ???"???, ????? hitInfo+0xD9 ??"????"???
// ?????????/???????, ??????????, ????:
//    ??? + ????????  = ??? / ???
//    ??? + ??????      = ?????????(????)
// ???? prologue ? 16 ??, ????????, ?????????????
static volatile unsigned long long g_resA2 = 0;
static volatile LONG g_resSeq = 0;
static volatile LONG g_resHookOk = 0;
static unsigned long long g_resCode = 0;
static volatile LONG g_resHitSnap = 0;   // ??????????(??????)
static volatile unsigned long long g_resRet = 0;   // ?? 0xB68D00 ?????(????????)
static volatile unsigned g_resA3v = 0;    // arg3 = ?????
static volatile unsigned g_resA4v = 0;    // arg4
static volatile unsigned long long g_playerHpAddr = 0;   // ?? ChrIns+0x130 ???(????)
static volatile LONG g_resHpSnap = 0;    // ?????????(??????)

static bool install_resolve_hook(void)
{
    unsigned long long tgt = g_base + 0xB68D00ULL;
    static const unsigned char expect[16] = {
        0x48,0x8B,0xC4, 0x48,0x89,0x58,0x08, 0x48,0x89,0x68,0x10,
        0x48,0x89,0x70,0x18, 0x57
    };
    unsigned char orig[16];
    unsigned char* p;
    unsigned char* dst;
    DWORD oldp = 0;
    if (!safe_read(tgt, orig, 16) || memcmp(orig, expect, 16) != 0) return false;
    g_playerHpAddr = (unsigned long long)(uintptr_t)&g_resHpSnap;   // ????, ???????
    p = (unsigned char*)VirtualAlloc(NULL, 0x200, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (p == NULL) return false;
    dst = p;
#define EMIT_MOV_RAX_IMM(a) do { dst[0]=0x48; dst[1]=0xB8; \
        *(unsigned long long*)(dst+2)=(unsigned long long)(uintptr_t)(a); dst += 10; } while (0)
    EMIT_MOV_RAX_IMM(&g_resA1); dst[0]=0x48; dst[1]=0x89; dst[2]=0x08; dst += 3;  // mov [rax],rcx
    EMIT_MOV_RAX_IMM(&g_resA2); dst[0]=0x48; dst[1]=0x89; dst[2]=0x10; dst += 3;  // mov [rax],rdx
    // ? ??"?????"(????)?0xB68D00 ?? 3 ????:
    //     0x9E6699 / 0xB6615A / 0xB6BFAD
    //   ??"???"?"????"???????, ????????, ??????
    //   ??????? prologue ???(prologue ?? push rdi, ?? rsp)?
    dst[0]=0x4C; dst[1]=0x8B; dst[2]=0x1C; dst[3]=0x24; dst += 4;               // mov r11,[rsp]
    EMIT_MOV_RAX_IMM(&g_resRet); dst[0]=0x4C; dst[1]=0x89; dst[2]=0x18; dst += 3; // mov [rax],r11
    // ? ? arg3 / arg4 ?? 0xB6BFAD ????????????????????"?????",
    //   ?? 3 / 0xC / 0x403 / 0x404 / 0x3E9 / 0xA?3 ?"????", 0x403/0x404 ?"????"?
    EMIT_MOV_RAX_IMM(&g_resA3v); dst[0]=0x44; dst[1]=0x89; dst[2]=0x00; dst += 3;  // mov [rax],r8d
    EMIT_MOV_RAX_IMM(&g_resA4v); dst[0]=0x44; dst[1]=0x89; dst[2]=0x08; dst += 3;  // mov [rax],r9d
    // ? ?"?????"??????????
    //   ???????????, ???? 2ms ?? ?? ???????,
    //   ????????????????????, ????????"???"?
    //   ? r11d ??????(???????, ??????????)?
    EMIT_MOV_RAX_IMM(&g_hitSeq);     dst[0]=0x44; dst[1]=0x8B; dst[2]=0x18; dst += 3;  // mov r11d,[rax]
    EMIT_MOV_RAX_IMM(&g_resHitSnap); dst[0]=0x44; dst[1]=0x89; dst[2]=0x18; dst += 3;  // mov [rax],r11d
    // ?? ????"??????"?
    //   ???????????: 0xBD4D40(????)? 10 ?????0xBD64E0(?HP)? 27 ?,
    //   ?????????? ?? ????????????, ???????
    //   ???????, ??????????
    //   g_playerHpAddr ???? g_resHpSnap ??(????), ????????
    EMIT_MOV_RAX_IMM(&g_playerHpAddr); dst[0]=0x48; dst[1]=0x8B; dst[2]=0x00; dst += 3;  // mov rax,[rax]
    dst[0]=0x44; dst[1]=0x8B; dst[2]=0x18; dst += 3;                                    // mov r11d,[rax]
    EMIT_MOV_RAX_IMM(&g_resHpSnap);    dst[0]=0x44; dst[1]=0x89; dst[2]=0x18; dst += 3; // mov [rax],r11d
    EMIT_MOV_RAX_IMM(&g_resSeq); dst[0]=0xF0; dst[1]=0xFF; dst[2]=0x00; dst += 3; // lock inc dword [rax]
#undef EMIT_MOV_RAX_IMM
    memcpy(dst, orig, 16); dst += 16;                       // ???? prologue, ????
    dst[0] = 0xFF; dst[1] = 0x25; *(unsigned int*)(dst + 2) = 0;
    *(unsigned long long*)(dst + 6) = tgt + 16; dst += 14;  // jmp [rip+0] -> tgt+16
    FlushInstructionCache(GetCurrentProcess(), p, (SIZE_T)(dst - p));
    {
        unsigned char patch[14];
        patch[0] = 0xFF; patch[1] = 0x25; *(unsigned int*)(patch + 2) = 0;
        *(unsigned long long*)(patch + 6) = (unsigned long long)(uintptr_t)p;
        if (!VirtualProtect((LPVOID)(uintptr_t)tgt, 16, PAGE_EXECUTE_READWRITE, &oldp)) return false;
        suspend_others(true);
        memcpy((void*)(uintptr_t)tgt, patch, 14);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)tgt, 16);
        suspend_others(false);
        VirtualProtect((LPVOID)(uintptr_t)tgt, 16, oldp, &oldp);
    }
    g_resCode = (unsigned long long)(uintptr_t)p;
    g_resHookOk = 1;
    return true;
}


static bool install_inv_hooks(void)
{
    // ????????, ????????
    static const unsigned char wantA[5] = {0xE8, 0x0A, 0x70, 0x07, 0x00};   // call 0x140BE00D0
    static const unsigned char wantB[5] = {0xE8, 0x8B, 0x70, 0x07, 0x00};   // call 0x140BE0170
    unsigned char got[5];
    unsigned long long A = g_base + 0xB690C1ULL, B = g_base + 0xB690E0ULL;
    g_realInvA = (PFN_INV)(uintptr_t)(g_base + 0xBE00D0ULL);
    g_realInvB = (PFN_INV)(uintptr_t)(g_base + 0xBE0170ULL);
    if (!safe_read(A, got, 5) || memcmp(got, wantA, 5) != 0) { g_invLog = 0; return false; }
    if (!safe_read(B, got, 5) || memcmp(got, wantB, 5) != 0) { g_invLog = 0; return false; }
    if (!patch_call_target(A, (void*)&inv_probe_a, &g_invStubA)) { g_invLog = 0; return false; }
    if (!patch_call_target(B, (void*)&inv_probe_b, &g_invStubB)) { g_invLog = 0; return false; }
    g_negHookOk = 1;
    g_invLog = 1;
    return true;
}

// ??????: chrwatch ??????? ~2Hz(???? 400~500ms),
// ?????????? ?? ???"???????????"????
// ??????????: ????(?????? IAT ????) + ???????
static DWORD WINAPI fastkey(LPVOID)
{
    LONG  prevK[8];
    int   i, init = 0;
    DWORD lastMode = 0, lastRing = 0, lastQuiet = 0, lastHist = 0, lastInv = 0;
    DWORD lastNegRep = 0;
    int   useArTrigger = 0;      // MODE ? 5act: ??????????
    DWORD lastResRep = 0;
    LONG  lastResSeq = 0;
    int   resPending = 0;
    DWORD resPendingAt = 0;
    LONG  resPendingHit = 0;
    LONG  lastInvLogged = 0;
    LONG  lastResLogged = 0;
    int   resPendingPlayer = 0;
    LONG  resPendingHp = 0;
    int   lastPlayerHp = -1;      // ????????: ?? = ?? = ??????
    DWORD lastHpDrop = 0;         // ?????????(????"???????")
    // ????????(?"?????"???)
    int   lastPAnim = -2;
    float lastPLen = 0.0f;
    DWORD lastPressAt = 0;
    int   pressPendingAccept = 0;
    int   pAnimLogged = 0;
    int   fastPlayer = 0;         // MODE ?? act ???
    // "????????" ?? ?**??**
    // ??????????(3002/3003...)????????(9999/405010/42000/0)?????,
    // ???? elen ?? 0.00~0.10 ????: ?????? >=150ms ?????????
    int   dbAnim = -1;            // ??????
    int   dbCand = -1;            // ?????
    DWORD dbCandAt = 0;
    DWORD dbLastChange = 0;       // ???"?????"???
    // ????: ??"????????????"????, ????????
    int   faOn = 0;
    int   faNeedRestore = 0;
    DWORD faUntil = 0;
    int   faAnim = -1;
    float faElen = 0.0f;
    int   faPendingOpen = 0;      // ???????(??????, ???????????"??")
    DWORD faOpenAt = 0;
    int   faPendAnim = -1;
    DWORD lastDodgePress = 0;
    // ★v94: "突破限制" 的状态
    DWORD psChkAt = 0, psWinStart = 0;
    int   psWasActive = 0, unlockKeyN = 0, psAnimId0 = -1;
    float psAnimT0 = 0.0f;
    int   s6PinOffLogged = 0;     // ★v94: "钉住被松开"只记一次
    int   psWinLogged = 0;        // ★v95: "本窗口没写倍速"只记一次
    int   wAnimLast = -1;         // ★v95: 窗口内"玩家动画变了"的观测
    DWORD wLastKeyAt = 0;
    // ?? v65: 6act ?"????"???(?????????, ???????
    //     ? 2ms ??????????????)
    int   s6Pend = 0;             // ??????, ???(??=?? / ??=??)
    DWORD s6At = 0;               // ??????
    LONG  s6Hit = 0;              // ???????"????"??
    int   s6WasIn = 0;            // ??? "???????????????"
    // ---- v76: 按下闪避 -> 待确认 -> 确认/否决 的状态机 ----
    int   s6Arm = 0;              // 1 = 刚按了闪避, 正在等"这一击有没有被吃掉"
    int   s6ArmAny = 0;           // 1 = 走的是近战/表窗口那条
    int   s6ArmAnim = -1;
    int   s6ArmCs = -1;
    unsigned int s6ArmModel = 0;
    unsigned long long s6ArmCtr = 0;
    int   s6ArmWin = 0;
    DWORD s6ArmAt = 0;
    LONG  s6ArmHit = 0;
    // ★v84: "同一刀只缓速一次" —— 记住上一次触发过的窗口身份
    unsigned long long s6LastCtr = 0;
    int   s6LastAnim = -1;
    int   s6LastWin = 0;
    int   s6LastCs = -1;
    DWORD s6HoldUntil = 0;        // v88: "钉住无敌帧"持续到这个时刻
    // ★v90
    DWORD s6BlockAt = 0;          // 最近一次"格挡掉攻击"的时刻(格挡不是闪避, 之后的闪避不该缓速)
    int   s6ShieldN = 0;          // 屏蔽伤害的次数(日志用)
    volatile LONG g_negAt = 0;    // 最近一次"攻击打到我头上但没掉血"(结算事件)的时刻
    int   s6Armed = 0;            // "??????????"(??????? / ?????)
    int   s6Fired = 0;            // ?????????????(????????)
    DWORD s6WinLog = 0;
    DWORD s6Trace = 0;
    unsigned long long s6LastAtk = 0;   // ???"????"?????(?? 1.5 ?)
    DWORD s6LastAtkT = 0;
    // fastkey ???????: ????????? 2ms ??
    DWORD fpLastTick = 0, fpPerfT = 0, fpGapMax = 0;
    LONG  fpPerfN = 0;
    double fpBodyMax = 0.0, fpSleepMax = 0.0, fpBlkMax = 0.0, fpPrevAfter = 0.0;
    LONG  lastNegN = 0;
    fopen_s(&g_slowF, SLOWPATH, "w");
    if (g_slowF) { fputs("# fastkey start\n", g_slowF); fflush(g_slowF); }
    // ?????????? 1ms, ?? Sleep(2) ???? 15.6ms
    {
        HMODULE hw = LoadLibraryA("winmm.dll");
        if (hw)
        {
            typedef UINT (WINAPI *PFN_TBP)(UINT);
            PFN_TBP p = (PFN_TBP)GetProcAddress(hw, "timeBeginPeriod");
            if (p) p(1);
        }
    }

    for (;;)
    {
        DWORD now;
        int   newDodge = 0, newActionVk = 0;
        // ?v71: ? Sleep ?"???"???????? ?? ????????? 200~470ms,
        //       ? 2ms ????, ???"?????????/?????"????
        {
            double t0 = qms();
            Sleep(2);
            {
                double t1 = qms();
                if (t1 - t0 > fpSleepMax) fpSleepMax = t1 - t0;
                if (fpPrevAfter > 0.0 && (t1 - fpPrevAfter) > fpBodyMax) fpBodyMax = t1 - fpPrevAfter;
                fpPrevAfter = t1;
            }
        }
        now = GetTickCount();
        if (!g_ready) continue;

        // ---- fastkey ????(? 2 ???) ----
        if (fpLastTick != 0)
        {
            DWORD gap = now - fpLastTick;
            if (gap > fpGapMax) fpGapMax = gap;
        }
        fpLastTick = now;
        fpPerfN++;
        if (fpPerfT == 0) fpPerfT = now;
        if (now - fpPerfT >= 2000)
        {
            if (g_slowF)
            {
                // ★v94: 顺便每 2 秒报一次"我们自己读到的玩家 pspeed" ——
                //   用来确认 (a) 平时确实是 1.00, (b) 窗口里写进去的 4.00 有没有留住。
                float psNow = -1.0f;
                if (!get_player_playspeed(&psNow)) psNow = -1.0f;
                fprintf(g_slowF, "t=%u FPERF iters=%ld period=%.2fms gapMax=%ums sleepMax=%.1fms bodyMax=%.1fms blk6Max=%.1fms fcCalls=%ld slow=%d slows=%ld aborts=%ld\n",
                        (unsigned)now, fpPerfN,
                        (double)(now - fpPerfT) / (double)(fpPerfN ? fpPerfN : 1),
                        (unsigned)fpGapMax, fpSleepMax, fpBodyMax, fpBlkMax,
                        (long)InterlockedExchange(&g_fcCalls, 0),
                        g_slowActive, (long)g_slowCount, (long)g_abortCount);
                fprintf(g_slowF, "t=%u UNLOCKSTAT on=%ld ps玩家=%.2f 窗口=%ld 窗口内按键=%ld 写=%ld\n",
                        (unsigned)now, (long)g_unlockOn, (double)psNow,
                        (long)g_unlockWinN, (long)g_unlockKeeps, (long)g_unlockWrites);
                fprintf(g_slowF, "t=%u FXSTAT on=%ld dead=%ld ready=%ld present=%ld draws=%ld fade=%.2f\n",
                        (unsigned)now, (long)fx_on, (long)fx_dead, (long)fx_ready,
                        (long)fx_seen, (long)fx_draws, (double)fx_fade);
                fprintf(g_slowF, "t=%u FMODSTAT 开关=%ld 钩子=%ld sys=%llX pitch=%.2f 目标=%.2f 次数=%ld\n",
                        (unsigned)now, (long)g_fmodOn, (long)g_fmodHookMode,
                        (unsigned long long)g_fmodSys, (double)g_fmodPitchNow,
                        (double)g_fmodPitchWant, (long)g_fmodSets);
                fflush(g_slowF);
            }
            fpPerfT = now; fpPerfN = 0; fpGapMax = 0;
            fpSleepMax = 0.0; fpBodyMax = 0.0; fpBlkMax = 0.0;
        }

        if (now - lastMode > 500)
        {
            lastMode = now;
            read_mode(g_mode, sizeof(g_mode));
            fastPlayer = (strstr(g_mode, "act") != NULL) ? 1 : 0;
            useArTrigger = (strstr(g_mode, "5act") != NULL) ? 1 : 0;
            // ★v94: "突破限制" 默认开; MODE 里写 nolk 关掉(只做 A/B 对比)
            g_unlockOn = (strstr(g_mode, "nolk") != NULL) ? 0 : 1;
            // ★v97: 倍速默认 1.5 —— 用户实测: 1.0 感觉不出"突破限制", 2.0 滑步太重。
            //   想换: ps1(不加速) / ps15 / ps2 / ps3 / ps4 (half = ps2, 老写法兼容)
            g_unlockPS = 1.5f;
            if      (strstr(g_mode, "ps4")  != NULL) g_unlockPS = 4.0f;
            else if (strstr(g_mode, "ps3")  != NULL) g_unlockPS = 3.0f;
            else if (strstr(g_mode, "ps2")  != NULL) g_unlockPS = 2.0f;
            else if (strstr(g_mode, "half") != NULL) g_unlockPS = 2.0f;
            else if (strstr(g_mode, "ps15") != NULL) g_unlockPS = 1.5f;
            else if (strstr(g_mode, "ps1")  != NULL) g_unlockPS = 1.0f;
            // ★v109: "成功闪避判断范围"的三颗旋钮(默认 = 一直以来的值)
            //   ① 余量: 实测射程 + 多少米还算够得着   ② 上限: 单招最多放宽到多少米
            //   ③ 容差: "学到的命中状态"允许的距离偏差
            g_reachMargin = 2.0f; g_reachCap = 4.5f; g_rngTol = 3.0f;
            if (strstr(g_mode, "tight") != NULL)      { g_reachMargin = 1.0f; g_reachCap = 3.5f; g_rngTol = 1.5f; }
            else if (strstr(g_mode, "loose") != NULL) { g_reachMargin = 3.0f; g_reachCap = 6.0f; g_rngTol = 4.5f; }
            // ★v96: 凸面镜特效开关(默认开); MODE 里写 nofx 关掉
            fx_on = (strstr(g_mode, "nofx") != NULL) ? 0 : 1;
            // ★v99: 提示音默认关(用户否掉了那个合成音效); 想听就写 sfxsting
            g_sfxOn = (strstr(g_mode, "sfxsting") != NULL) ? 1 : 0;
            // ★v99: 游戏音效缓速(FMOD 降调)开关与强度
            g_fmodOn = (strstr(g_mode, "nofmod") != NULL) ? 0 : 1;
            if      (strstr(g_mode, "ap9")  != NULL) g_fmodPitchWant = 0.90f;
            else if (strstr(g_mode, "ap8")  != NULL) g_fmodPitchWant = 0.80f;
            else if (strstr(g_mode, "ap75") != NULL) g_fmodPitchWant = 0.75f;
            else if (strstr(g_mode, "ap6")  != NULL) g_fmodPitchWant = 0.60f;
            else if (strstr(g_mode, "ap5")  != NULL) g_fmodPitchWant = 0.50f;
            else if (strstr(g_mode, "ap4")  != NULL) g_fmodPitchWant = 0.40f;
            else                                     g_fmodPitchWant = 0.65f;   // ★v100 默认再降
        }

        // 0) ??"????"?? [[WorldChrMan+0x88]+0x1FF8]+0x88 ?? ?????????????
        if (now - lastInv > 500)
        {
            lastInv = now;
            unsigned long long wcm = 0, man = 0, c = 0, obj = 0;
            if (rd_ptr(g_base + 0x3D7A1E0ULL, &wcm) && wcm != 0 &&
                rd_ptr(wcm + 0x88, &man) && man != 0 &&
                rd_ptr(man + 0x1FF8, &c) && c != 0 &&
                rd_ptr(c + 0x88, &obj) && obj != 0)
                g_playerInv = obj;
            if (man != 0) g_playerMan = man;
            {
                unsigned long long pch = player_chr();
                if (pch != 0) g_playerHpAddr = pch + 0x130;   // ???????????
            }
        }

        // 0b) ???"????"???????(??)
        {
            LONG n = g_invN;
            if (n - lastInvLogged > 24) lastInvLogged = n - 24;
            while (lastInvLogged < n)
            {
                int i = (int)(lastInvLogged & 23);
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u INVOBJ obj=%llX r=%d %s\n", (unsigned)now,
                            g_invObj[i], g_invRes[i],
                            (g_invObj[i] == g_playerInv) ? "<== PLAYER" : "");
                    fflush(g_slowF);
                }
                lastInvLogged++;
            }
        }

        // ?????"????"??????? + ?????
        {
            static int invLogged = 0;
            if (!invLogged)
            {
                invLogged = 1;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u HOOKS resolve=%ld (1=on,0=off,-1=skipped by MODE) code=%llX\n",
                            (unsigned)now, (long)g_resHookOk, g_resCode);
                    fprintf(g_slowF, "t=%u HOOKS2 attackres=%ld (1=on,0=FAILED,-1=skipped)\n",
                            (unsigned)now, (long)g_arHookOk);
                    fflush(g_slowF);
                }
            }
        }

        // 1) ????ID ???? (16ms, ???? ~2s)
        if (now - lastRing >= 16)
        {
            unsigned long long ec = enemy_ctr_fast();
            lastRing = now;
            anim_push(now, (ec != 0) ? anim_of_ctr(ec, NULL) : -1);
            // ???"????????": ? fastkey ????
            {
                int curn = (ec != 0) ? anim_of_ctr(ec, NULL) : -1;
                if (curn >= 0)
                {
                    if (curn == dbAnim) dbCand = dbAnim;
                    else if (curn != dbCand) { dbCand = curn; dbCandAt = now; }
                    else if ((now - dbCandAt) >= 150)
                    {
                        dbAnim = curn;
                        dbLastChange = now;
                        if (g_slowF && strstr(g_mode, "4act") != NULL)
                        {
                            fprintf(g_slowF, "t=%u EANIM-STABLE %d (?????)\n", (unsigned)now, curn);
                            fflush(g_slowF);
                        }
                    }
                }
            }
                    // ?????????????(??"?????")
                    if (g_slowF)
            {
                unsigned long long pc2 = ctr_of_player();
                if (pc2 != 0)
                {
                    float len2 = 0.0f;
                    int   aid2 = anim_of_ctr(pc2, &len2);
                    int   act2 = -1;
                    {
                        unsigned long long ch2 = player_chr();
                        if (ch2 != 0) safe_read(ch2 + 0x230, &act2, 4);
                    }
                    if (aid2 >= 0 && (!pAnimLogged || aid2 != lastPAnim || (lastPLen - len2) > 0.10f))
                    {
                        if (pAnimLogged)
                        {
                            fprintf(g_slowF, "t=%u PACT anim %d->%d elen %.2f->%.2f act=%d\n",
                                    (unsigned)now, lastPAnim, aid2, lastPLen, len2, act2);
                            fflush(g_slowF);
                        }
                        pAnimLogged = 1;
                        lastPAnim = aid2;
                        if (pressPendingAccept && (now - lastPressAt) <= 320)
                        {
                            pressPendingAccept = 0;
                            fprintf(g_slowF, "t=%u PRESS-ACCEPTED delta=%ums anim=%d\n",
                                    (unsigned)now, (unsigned)(now - lastPressAt), aid2);
                            fflush(g_slowF);
                            fp_dump_player("ACCEPT", now, now - lastPressAt);
                        }
                    }
                    lastPLen = len2;
                }
            }
        }

        // 1b) ?? ChrIns ? 0x300 ????? (100ms, ???? 1.6 ?)
        if (now - lastHist >= 100)
        {
            unsigned long long ec = enemy_ctr_best();
            unsigned long long ech = 0;
            lastHist = now;
            if (ec != 0 && rd_ptr(ec + 0x18, &ech) && ech != 0)
            {
                int slot = g_histN % HRING;
                if (safe_read(ech, g_hist[slot], 0x300))
                {
                    unsigned long long ao = 0;
                    g_histChr[slot] = ech;
                    g_histT[slot] = now;
                    g_histAO[slot] = 0;
                    if (rd_ptr(ec + 0x10, &ao) && ao != 0 && safe_read(ao, g_histA[slot], 0x100))
                        g_histAO[slot] = ao;
                    g_histN++;
                }
            }
        }

        // 2) ?? (g_keyDown ? IAT ??? GetAsyncKeyState ????)
        // ?v71: ???????, ?????????????? ?? ????????
        //        (??????????) ???, ?????????
        {
            static int sPrevDirect = 0;
            SHORT ksD = GetAsyncKeyState(DODGE_VK);
            SHORT ksG = GetAsyncKeyState(0x10);
            int dNow = ((ksD & 0x8000) || (ksG & 0x8000)) ? 1 : 0;
            if (dNow && !sPrevDirect)
            {
                newDodge = 1;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u PRESS(direct) vk=0x%X/0x10\n", (unsigned)now, DODGE_VK);
                    fflush(g_slowF);
                }
            }
            sPrevDirect = dNow;
        }
        if (!init) { for (i = 0; i < 8; i++) prevK[i] = g_keyDown[i]; init = 1; }
        for (i = 0; i < 8; i++)
        {
            LONG added = g_keyDown[i] & ~prevK[i];
            prevK[i] = g_keyDown[i];
            while (added)
            {
                int b;
                for (b = 0; b < 32; b++) if (added & (1L << b)) break;
                if (b >= 32) break;
                {
                    int vk = (i << 5) + b;
                    added &= ~(1L << b);
                    if (vk == DODGE_VK || vk == 0x10 || vk == 0xA0 || vk == 0xA1) newDodge = 1;
                    if (!is_move_key(vk) && newActionVk == 0) newActionVk = vk;
                }
            }
        }

        // ????(slow ??)?????? chrwatch ?, ?????
        if (is_slow_mode(g_mode)) continue;
        // ????????????(?????????????? 1.0)
        if (!g_slowActive && g_gameSpeed != 1.0) set_game_speed(1.0);

        // ★v99: 缓速开始/结束 -> 游戏音频的 pitch 跟着降/还原(FMOD)
        {
            int sl = (g_slowActive != 0);

            // ★v100: 空中作废 —— 义父踩你上天之后空按闪避也会起缓速, 就是这个漏
            if (sl) air_update(now);
            // ★v108: 不只是"刚起缓速那 250ms", 缓速期间**任何时候被弄上天**(义父的踩)
            //   也算受击 -> 缓速作废。(跳是按键, 本来就会当场结束, 所以不冲突)
            if (sl && g_playerAir)
            {
                g_slowActive = 0; g_abortActive = 0;
                set_game_speed(1.0);
                InterlockedIncrement(&g_abortCount);
                sl = 0;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u AIR-VETO: 人在空中(被踩飞/跳起来) -> 缓速作废 (窗口 %ums)\n",
                            (unsigned)now, (unsigned)(now - g_slowStart));
                    fflush(g_slowF);
                }
            }
            // ★★ v118: 取消链 —— 这次缓速如果是"技能被垫步取消"出来的, 换成强化版
            // ★v127: 提示音的平滑淡出 —— 缓速结束后 200ms 里把 MCI 音量台阶式降到 0 再停
            if (g_stingFading)
            {
                typedef DWORD (WINAPI *MCI_t)(LPCSTR, LPSTR, UINT, HWND);
                static MCI_t mciV = NULL;
                static LONG triedV = 0;
                DWORD el2;
                if (InterlockedCompareExchange(&triedV, 1, 0) == 0)
                {
                    HMODULE hV = LoadLibraryA("winmm.dll");
                    if (hV) mciV = (MCI_t)GetProcAddress(hV, "mciSendStringA");
                }
                el2 = (DWORD)(now - g_stingFadeAt);
                if (mciV)
                {
                    char cmd[64];
                    if (el2 < 200)
                    {
                        int vol = (int)(1000.0f * (1.0f - (float)el2 / 200.0f));
                        if (vol < 0) vol = 0;
                        sprintf(cmd, "setaudio fxwarp volume to %d", vol);
                        mciV(cmd, NULL, 0, NULL);
                    }
                    else
                    {
                        mciV("stop fxwarp", NULL, 0, NULL);
                        mciV("setaudio fxwarp volume to 1000", NULL, 0, NULL);   // 还原音量给下一次
                        g_stingFading = 0;
                    }
                }
                else g_stingFading = 0;
            }

            // ★v120: 持续压技能动画(写一次会被引擎盖回去) —— 每帧都写, 直到 200ms 结束
            if (g_skillCutUntil != 0 && (LONG)(g_skillCutUntil - now) > 0)
            {
                unsigned long long pcs = ctr_of_player(), obs = 0;
                int aids = -1;
                float ts = 0.0f;
                if (pcs != 0 && rd_ptr(pcs + 0x10, &obs) && obs != 0 &&
                    rd_u32(obs + 0x20, &aids) && aids == g_skillCutAnim &&
                    safe_read(obs + 0x24, &ts, 4) && ts < 3.0f)
                {
                    float t2s = ts + 0.5f;      // 每次往前推一点, 压着它走完
                    safe_write(obs + 0x24, &t2s, 4);
                }
                else if (aids != g_skillCutAnim) g_skillCutUntil = 0;   // 动作已经换了 -> 收工
            }
            else if (g_skillCutUntil != 0 && (LONG)(g_skillCutUntil - now) <= 0) g_skillCutUntil = 0;

            //   (更慢 0.18 倍 + 更长 1200ms)。只在窗口刚开 80ms 内升级一次。
            // ★★ v123: 用户判定"取消链达不到完善" -> **默认关掉**, 只有 MODE 里写 chain 才启用。
            //   (数据: 收紧后 SKILL-CUT 仍然 27 次/93 窗口 —— 79xxxx 这些 id 里显然混了
            //    不是技能的动作, 想做成"针对性"必须逐招验证 id, 那是后面的活。)
            if (strstr(g_mode, "chain") != NULL &&
                g_slowActive && g_cancelDodgeAt != 0 &&
                (DWORD)(now - g_cancelDodgeAt) <= 150 &&      // ★v120: 收紧(500ms 太宽, 普通闪避也被强化了)
                (DWORD)(now - g_slowStart) <= 80 &&
                g_slowSpeedNow > 0.11f)
            {
                // ★v121: 0.10/1500 太长太狠(而且会被残留), 改成 0.12 倍 / 1100ms —— 明显更慢,
                //   但不会把你困在慢动作里
                g_slowSpeedNow = 0.12f;
                g_slowDurNow = 1100;
                set_game_speed(g_slowSpeedNow);
                InterlockedIncrement(&g_cancelChainN);
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u >>> CANCEL-CHAIN 强化缓速: 技能被垫步取消 -> 0.18 倍 / %ums\n",
                            (unsigned)now, (unsigned)g_slowDurNow);
                    fflush(g_slowF);
                }
            }

            // ★★ v116: 义父的踩的真实信号 —— 玩家进了"受击动画"。
            {
                unsigned long long phc = ctr_of_player(), pho = 0;
                int pha = -1;
                if (phc != 0 && rd_ptr(phc + 0x10, &pho) && pho != 0 && rd_u32(pho + 0x20, &pha) && pha >= 0)
                {
                    if ((DWORD)(now - g_hurtLearnUntil) < 0x80000000u && now < g_hurtLearnUntil)
                        hurt_learn(pha);                       // 挨刀后这 250ms 里出现的动画 = 受击动画
                    if (g_slowActive && hurt_is(pha))
                    {
                        g_slowActive = 0; g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                        sl = 0;
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u HURT-CANCEL: 我进了受击动画(%d) -> 缓速作废\n",
                                    (unsigned)now, pha);
                            fflush(g_slowF);
                        }
                    }
                }
            }

            // ★★ v115: 投技/抓取(义父的踩这类)无视垫步无敌帧, 也不吃普通"无敌"判定 ——
            //   缓速期间只要敌人在出投技(type 304, 我们从游戏档案里解出来的), 缓速就作废。
            if (g_slowActive)
            {
                int gi;
                for (gi = 0; gi < 4; gi++)
                {
                    unsigned long long gc = g_s6Near[gi], gob = 0;
                    unsigned int gmodel = 0, gvar = 0;
                    int ganim = -1, gnpc = -1;
                    unsigned short gs = 0, ge = 0;
                    short gj = 0;
                    float gt = 0.0f;
                    if (gc == 0) continue;
                    gmodel = model_of_ctr(gc, &gvar, &gnpc, now);
                    if (gmodel == 0 || !rd_ptr(gc + 0x10, &gob) || gob == 0) continue;
                    if (!rd_u32(gob + 0x20, &ganim) || ganim < 0) continue;
                    if (!safe_read(gob + 0x24, &gt, 4)) continue;
                    if (ev_find(gmodel, ganim, 304, (int)(gt * 100.0f + 0.5f), &gs, &ge, &gj))
                    {
                        g_slowActive = 0; g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                        sl = 0;
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u GRAB-CANCEL: 敌人在出投技/抓取(踩这类) -> 缓速作废 anim=%d\n",
                                    (unsigned)now, ganim);
                            fflush(g_slowF);
                        }
                        break;
                    }
                }
            }

            if (sl != fmodWasSlow)
            {
                if (!sl)
                {
                    g_slowOffAt = now; g_fadeFrom = fx_fade;   // ★v101 记下淡出起点
                    // ★★ v121: 关键修复 —— 任何结束路径(受击取消/空中/投技/受击动画/超时/按键)
                    //   都要把"这一窗的参数"复位。之前只在"正常结束"里复位, 于是强化缓速
                    //   (0.10 倍/1500ms)会**残留到之后的每一次普通闪避** -> 全程都像慢动作,
                    //   手感就"变菜"了。
                    g_slowSpeedNow = SLOW_SPEED;
                    g_slowDurNow   = SLOW_MS;
                    fx_sting_stop();          // ★v126: 缓速一结束就掐掉提示音(不再拖到你下一步动作之后)
                }
                else     { g_slowOffAt = 0; }
                // ★v124: 音效 —— 窗口开始/结束各一次(不走补丁, 直接对 FMOD 的通道组改频率)
                if (g_fmodScanSys != 0)
                {
                    fmod_override_now(sl ? g_fmodPitchWant : 1.0f);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u FMOD-SCAN 音频 %.2f (次数=%ld)\n",
                                (unsigned)now, (double)(sl ? g_fmodPitchWant : 1.0f), (long)g_fmodSets);
                        fflush(g_slowF);
                    }
                }
                fmodWasSlow = sl;
                if (g_fmodOn)
                {
                    // ★v103: 这里只写"我想要多少", 真正的 FMOD 调用在游戏线程的 update 钩子里做
                    g_fmodWant = sl ? g_fmodPitchWant : 1.0f;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u FMOD 请求 %.2f (等游戏线程执行, 钩子=%ld)\n",
                                (unsigned)now, (double)g_fmodWant, (long)g_fmodHookMode);
                        fflush(g_slowF);
                    }
                }
            }
        }

        // ★★ v94: "突破限制" —— 缓速窗口内让**玩家自己**保持正常速度
        //   世界被 set_game_speed(0.25) 放慢时, 玩家自己的动画也一起慢 4 倍:
        //   垫步动画的取消点要等 4 倍现实时间才到, 这期间攻击/技能/忍具全被
        //   动作锁定吃掉 —— 这就是"限制"。把玩家 PlaySpeed 写成 1/0.25 = 4.0,
        //   玩家的动作就按正常速度走完(相对世界的速度回到 1.0, 无敌帧比例不变),
        //   于是"成功闪避后那 1 秒"里能真的打出攻击/寄鹰斩/忍具。
        //   窗口一关立刻写回 1.0; 非窗口期绝不碰这个地址。
        {
            int psActiveNow = (g_slowActive && g_unlockOn) ? 1 : 0;
            if (psActiveNow && !psWasActive)
            {
                psWinStart = now;
                psChkAt = 0;              // 立刻写一次
                unlockKeyN = 0;
                s6PinOffLogged = 0;
                psWinLogged = 0;
                wAnimLast = -1;
                wLastKeyAt = 0;
                psAnimId0 = -1; psAnimT0 = 0.0f;
                {
                    unsigned long long pcw = ctr_of_player();
                    if (pcw != 0) psAnimId0 = anim_of_ctr(pcw, &psAnimT0);
                }
                InterlockedIncrement(&g_unlockWinN);
                // ★v98: 闪避成功这一瞬间 -> 放"时间拉长"提示音
                // ★v129: 如果按闪避那一刻已经放过(提前版), 这里就不再放第二次
                if (g_stingPlayedAt == 0 || (DWORD)(now - g_stingPlayedAt) > 400)
                {
                    fx_sting();
                    g_stingPlayedAt = now;
                }
            }
            psWasActive = psActiveNow;

            if (psActiveNow)
            {
                if (g_unlockPS <= 1.0f)
                {
                    // ★v95: 倍速取消了 —— 完全不碰玩家的 pspeed, 只保留
                    //   "窗口内按键不关缓速" + "按键就松开钉住" 这两条解锁手段。
                    if (g_slowF && !psWinLogged)
                    {
                        psWinLogged = 1;
                        fprintf(g_slowF, "t=%u UNLOCK 本窗口不动玩家倍速(已取消), 只做 按键解锁+松开钉住\n",
                                (unsigned)now);
                        fflush(g_slowF);
                    }
                }
                else
                {
                // 每次都写: 游戏有可能每帧把这个值盖回去, 写一次未必留得住
                set_player_playspeed(g_unlockPS);
                InterlockedIncrement(&g_unlockWrites);
                g_psLast = g_unlockPS;
                if ((now - psChkAt) >= 200)
                {
                    psChkAt = now;
                    float cur = 0.0f;
                    if (get_player_playspeed(&cur))
                    {
                        if (g_slowF)
                        {
                            // cur 若 ≈1.00 说明游戏一直在把我们的写入盖回去(那这条路的收益就有限)
                            fprintf(g_slowF, "t=%u UNLOCK pspeed 现读=%.2f (我们要 %.2f, 已写 %ld 次)\n",
                                    (unsigned)now, cur, (double)g_unlockPS, (long)g_unlockWrites);
                            fflush(g_slowF);
                        }
                    }
                    else if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u UNLOCK pspeed 读不到(地址无效?)\n", (unsigned)now);
                        fflush(g_slowF);
                    }
                }
                }   // end 倍速分支
            }
            else if (g_psLast != 1.0f)
            {
                set_player_playspeed(1.0f);
                if (g_slowF && psWinStart != 0)
                {
                    float t1 = 0.0f;
                    int   id1 = -1;
                    unsigned long long pcw = ctr_of_player();
                    if (pcw != 0) id1 = anim_of_ctr(pcw, &t1);
                    {
                        DWORD el2 = now - psWinStart;
                        float dam = (psAnimId0 == id1 && el2 > 0) ? (t1 - psAnimT0) : -1.0f;
                        fprintf(g_slowF, "t=%u UNLOCK 还原 pspeed -> 1.00 (窗口 %ums, 玩家动画 %d->%d Δanim=%.3f 每毫秒%.5f => %s)\n",
                                (unsigned)now, (unsigned)el2, psAnimId0, id1,
                                (double)dam, (double)((el2 > 0) ? dam / (float)el2 : -1.0f),
                                (dam > 0.0f && (dam / (float)(el2 ? el2 : 1)) > 0.0007f)
                                    ? "约等于正常速度(解锁生效)" : "看起来还是被放慢(解锁没生效?)");
                    }
                    fflush(g_slowF);
                }
                psWinStart = 0;
                g_psLast = 1.0f;
            }

            // ★v95: 窗口内观测"玩家的动画什么时候真的变了" —— 这是判断
            //   "我按了攻击, 动作到底有没有出来"的硬指标(不用靠感觉)。
            if (psActiveNow)
            {
                int   aNow = -1;
                float tNow = 0.0f;
                unsigned long long pcw2 = ctr_of_player();
                if (pcw2 != 0) aNow = anim_of_ctr(pcw2, &tNow);
                if (wAnimLast > 0 && aNow > 0 && aNow != wAnimLast && g_slowF)
                {
                    // ★v117: 取消链标记 —— 技能动画(79xxxx / 5xxxxxxxx) -> 垫步(213300~213304)
                    {
                        int skillFrom = (wAnimLast >= 790000 && wAnimLast < 800000) || wAnimLast >= 50000000;
                        int dodgeTo   = (aNow >= 213300 && aNow <= 213304);
                        if (skillFrom && dodgeTo)
                        {
                            g_cancelDodgeAt = now;      // ★v118: 记下来, 给"取消链强化缓速"用
                            InterlockedIncrement(&g_cancelChainN);
                            fprintf(g_slowF, "t=%u CANCEL-DODGE 技能被垫步取消: %d -> %d (窗口 %ums)\n",
                                    (unsigned)now, wAnimLast, aNow, (unsigned)(now - psWinStart));
                        }
                    }
                    if (wLastKeyAt)
                        fprintf(g_slowF, "t=%u WACT 玩家动画 %d -> %d (窗口 %ums, 距按键 %ums)\n",
                                (unsigned)now, wAnimLast, aNow, (unsigned)(now - psWinStart),
                                (unsigned)(now - wLastKeyAt));
                    else
                        fprintf(g_slowF, "t=%u WACT 玩家动画 %d -> %d (窗口 %ums)\n",
                                (unsigned)now, wAnimLast, aNow, (unsigned)(now - psWinStart));
                    fflush(g_slowF);
                }
                if (aNow > 0) wAnimLast = aNow;

                // ★v117: 观测行 —— 窗口内每 120ms 记一次"我 vs 最近敌人"
                //   (义父的踩到底发生了什么, 就是要靠这几行看出来)
                {
                    static DWORD lastObsAt = 0;
                    if (g_slowF && (DWORD)(now - lastObsAt) >= 120)
                    {
                        float pp[3], me[3];
                        int eAnim = -1;
                        float ed = -1.0f;
                        lastObsAt = now;
                        if (pcw2 != 0 && ctr_pos(pcw2, pp))
                        {
                            unsigned long long ec = enemy_ctr_best();
                            if (ec != 0 && ctr_pos(ec, me))
                            {
                                float dx = me[0]-pp[0], dy = me[1]-pp[1], dz = me[2]-pp[2];
                                ed = sqrtf(dx*dx + dy*dy + dz*dz);
                            }
                        }
                        eAnim = -1;
                        {
                            unsigned long long ec2 = enemy_ctr_best();
                            if (ec2 != 0) eAnim = anim_of_ctr(ec2, NULL);
                        }
                        fprintf(g_slowF, "t=%u OBS 我anim=%d animT=%.2f y=%.2f | 敌anim=%d d=%.2f 窗口=%ums\n",
                                (unsigned)now, aNow, (double)tNow, (double)pp[1], eAnim, (double)ed,
                                (unsigned)(now - psWinStart));
                        fflush(g_slowF);
                    }
                }
            }
            else
            {
                wAnimLast = -1;
            }
        }

        // 3) ?????? -> ?"???"??? (???????"?????"??)
        if (InterlockedExchange(&g_hitPending, 0))
        {
            unsigned long long am = g_hitAtkMan;
            unsigned long long ac = ctr_of_attack_man(am);
            if (ac != 0)
            {
                float ppos[3] = {0, 0, 0}, epos[3] = {0, 0, 0}, dd = -1.0f;
                unsigned long long pc = ctr_of_player(), ach = 0;
                if (pc != 0 && ctr_pos(pc, ppos) && ctr_pos(ac, epos))
                {
                    float dx = epos[0]-ppos[0], dy = epos[1]-ppos[1], dz = epos[2]-ppos[2];
                    dd = sqrtf(dx*dx + dy*dy + dz*dz);
                }
                // ?v68: ??"?????" + "????????????(=?????????)"
                g_lastHitCtr = ac; g_lastHitCtrT = now;
                {
                    unsigned int vm = 0, vv2 = 0;
                    int vnpc = -1;
                    vm = model_of_ctr(ac, &vv2, &vnpc, now);
                    if (vm != 0 && dd >= 0.0f)
                    {
                        // ★v87: 近战射程只能用"近战命中"来学。
                        //   以前所有命中都算, 于是箭打中我们(远距离)会把射程灌到 8 米上限,
                        //   接着 5 米外的空放闪避也被当成"够得着" —— 用户报的"飞渡浮舟
                        //   离老远空放还触发缓速"就是这么来的。
                        //   判据: 攻击者此刻的动画里有没有"攻击判定帧(hitbox)事件" ->
                        //         有 = 近战, 学射程; 只有放弹丸事件 = 远程, 不学射程。
                        {
                            unsigned long long hob = 0;
                            int  hAnim = -1, isMelee = 0;
                            if (rd_ptr(ac + 0x10, &hob) && hob != 0) rd_u32(hob + 0x20, &hAnim);
                            if (hAnim >= 0)
                            {
                                unsigned short s1 = 0, e1 = 0, s2b = 0, e2b = 0;
                                short j1 = 0, j2b = 0;
                                if (ev_find(vm, hAnim, 1, 0, &s1, &e1, &j1)) isMelee = 1;
                                else if (ev_find(vm, hAnim, 2, 0, &s2b, &e2b, &j2b)) isMelee = 0;
                                else isMelee = (dd <= 4.0f) ? 1 : 0;   // 表里都没有: 近距离当近战
                            }
                            if (isMelee) { reach_note(vm, dd); if (hAnim >= 0) reach_note2(vm, hAnim, dd); }
                            else if (g_slowF)
                            {
                                fprintf(g_slowF, "   REACH 跳过(远程命中 anim=%d d=%.2f, 不学近战射程)\n",
                                        hAnim, dd);
                                fflush(g_slowF);
                            }
                        }
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "   REACH model=%u ????????=%.2f -> ?? %.2f\n",
                                    vm, dd, (double)reach_gate(vm, 0));
                            fflush(g_slowF);
                        }
                        // v75: 每一次"打中我们"都记 (不再要求 >=5m)。
                        //  7.5 米外的近战突进会把远程表污染成近战, 而按距离分类根本分不开;
                        //  干脆统一记成"命中状态" —— 近战/箭/苦无在命中那一刻的射手状态都是唯一的。
                        {
                            unsigned long long aob = 0;
                            int   aAnim = -1;
                            float aT = 0.0f;
                            if (rd_ptr(ac + 0x10, &aob) && aob != 0)
                            {
                                rd_u32(aob + 0x20, &aAnim);
                                safe_read(aob + 0x24, &aT, 4);
                            }
                            if (aAnim >= 0)
                            {
                                int acs = (int)(aT * 100.0f + 0.5f);
                                rng_note(vm, aAnim, acs, dd);
                                if (g_slowF)
                                {
                                    fprintf(g_slowF, "   RNG 学到命中状态 model=%u anim=%d cs=%d (被打中时距离 %.2f)\n",
                                            vm, aAnim, acs, dd);
                                    fflush(g_slowF);
                                }
                            }
                        }
                    }
                }
                fp_dump("HIT", now, dd, ac, am);
                // ??"????????": ??????????????
                {
                    int hid = anim_of_ctr(ac, NULL);
                    int cid = -1, npc = -1, th = -1;
                    int tk = chr_type(am, &cid, &npc, &th);
                    AtkSet* gs = at_global();
                    AtkSet* ts = (tk != 0) ? at_get(tk, 1) : NULL;
                    unsigned long long ach2 = 0;
                    at_add(gs, hid);
                    if (ts != NULL && ts != gs) at_add(ts, hid);
                    if (rd_ptr(ac + 0x18, &ach2) && ach2 != 0) cmap_put(ach2, tk);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "   hitAnim=%d type=%d cid=%d npc=%d think=%d  -100=%d -200=%d -300=%d -400=%d -500=%d\n",
                                hid, tk, cid, npc, th,
                                anim_at(now, 100, NULL), anim_at(now, 200, NULL),
                                anim_at(now, 300, NULL), anim_at(now, 400, NULL),
                                anim_at(now, 500, NULL));
                        fflush(g_slowF);
                    }
                }
                // ??"??? 1.6 ?"? ChrIns ????? ?? ??????????
                if (rd_ptr(ac + 0x18, &ach) && ach != 0 && g_slowF != NULL && g_histN > 0)
                {
                    int k;
                    for (k = 0; k < HRING; k++)
                    {
                        int idx = g_histN - 1 - k;
                        if (idx < 0) break;
                        idx %= HRING;
                        if (g_histChr[idx] != ach) continue;
                        fprintf(g_slowF, "== FP HITP%d t=%u age=%ums ctr=%llX man=%llX\n",
                                k, (unsigned)g_histT[idx],
                                (unsigned)(now - g_histT[idx]), ac, am);
                        fp_hex_mem(g_slowF, "   C", g_hist[idx], 0x300);
                        if (g_histAO[idx] != 0)
                            fp_hex_mem(g_slowF, "   A", g_histA[idx], 0x100);
                    }
                    fflush(g_slowF);
                }
            }
            else if (g_slowF)
            {
                fprintf(g_slowF, "t=%u HIT but no container (man=%llX)\n", (unsigned)now, am);
                fflush(g_slowF);
            }
        }

        // 4) ???????? (?????????, 2 ???)
        if (now - lastQuiet > 2000)
        {
            lastQuiet = now;
            if (!g_slowActive)
            {
                unsigned long long ec = enemy_ctr_best();
                if (ec != 0)
                {
                    float ppos[3] = {0, 0, 0}, epos[3] = {0, 0, 0}, dd = -1.0f;
                    unsigned long long pc = ctr_of_player();
                    if (pc != 0 && ctr_pos(pc, ppos) && ctr_pos(ec, epos))
                    {
                        float dx = epos[0]-ppos[0], dy = epos[1]-ppos[1], dz = epos[2]-ppos[2];
                        dd = sqrtf(dx*dx + dy*dy + dz*dz);
                    }
                    if (dd >= 0.0f && dd < 6.0f) fp_dump("QUIET", now, dd, ec, 0);
                }
            }
        }

        // 5a) ?????? (????"????????"???, ???????)
        // 5z) ????: ?????"???? + ???????????????"
        //     ?? sp=0.10 ? calls ?? -> ???????????????? -> ????
        {
            static DWORD lastClk = 0;
            if (now - lastClk > 1000)
            {
                lastClk = now;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u CLK tscale=%.3f gamesp=%.3f tsAddr=%llX calls=%ld slow=%d\n",
                            (unsigned)now, (double)g_gameSpeed, (double)g_gameSpeed, g_tsAddr,
                            (long)InterlockedCompareExchange(&g_clkCalls, 0, 0),
                            g_slowActive);
                    fflush(g_slowF);
                }
            }
        }
        // 4-9) ? ???"????"?? (0xB69000 ??) ?? ??????????????
        if (g_arHookOk == 1)
        {
            static LONG lastAr = 0;
            static int  arPending = 0;
            static DWORD arPendingAt = 0;
            static LONG arPendingHit = 0;
            LONG an = g_arSeq;
            if (an != lastAr)
            {
                lastAr = an;
                // ?? ????(? hook.txt ????????):
                //     a1 = ???(???), a2 = ???(???)
                //    ????? a2 -> ??"?????????", ????:
                //      ???/??? -> ??(?) ; ???? -> ???(?)
                //    ????: ? a1 ????? ChrIns ?????
                unsigned long long ta1 = g_arA1, ta2 = g_arA2;
                int isPlayer = 0;
                {
                    // a1 = ???, a2 = ??? (hook.txt ????)
                    // ? ??: a1 **??**? [a1+0x1FF8] ??(??? Man, v1ch ?? 0)?
                    //   ????: ? hook_c ???"????????? a1 ??"?????
                    //   ????????????, ?????
                    unsigned long long a1c = 0, a1ch = 0, pch0 = player_chr();
                    unsigned long long a2c = 0, a2ch = 0;
                    if (g_playerA1 != 0 && ta1 == g_playerA1) isPlayer = 1;
                    if (!isPlayer && ta1 != 0 && rd_ptr(ta1 + 0x1FF8, &a1c) && a1c != 0 &&
                        rd_ptr(a1c + 0x18, &a1ch) && a1ch != 0 && pch0 != 0 && a1ch == pch0)
                        isPlayer = 1;
                    if (ta2 != 0 && rd_ptr(ta2 + 0x1FF8, &a2c) && a2c != 0 &&
                        rd_ptr(a2c + 0x18, &a2ch) && a2ch != 0 && pch0 != 0 && a2ch == pch0)
                        isPlayer = 0;      // ?????? -> ?????, ????
                    g_dbgA1ch = a1ch;
                    g_dbgA2ch = a2ch;
                    g_dbgPch = pch0;
                }
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u AR#%ld victim1=%llX attacker2=%llX a3=0x%X a4=0x%X retRVA=0x%llX %s  [pA1=%llX v1ch=%llX a2ch=%llX me=%llX]\n",
                            (unsigned)now, an, g_arA1, g_arA2, g_arA3, g_arA4,
                            (unsigned long long)(g_arRet - g_base),
                            isPlayer ? "VICTIM=PLAYER" : "other",
                            g_playerA1, g_dbgA1ch, g_dbgA2ch, g_dbgPch);
                    fflush(g_slowF);
                }
                // ?? 5act: ?"???? + ???"?????????
                //    a2 = ?????; ???? => ???????????
                //    ?? 30ms ????????? => ?????????? => ????
                //    ?????????? -> ????? AR -> ????
                if (useArTrigger && isPlayer)
                {
                    arPending = 1;
                    arPendingAt = now;
                    arPendingHit = g_arHitSnap;
                    if (!g_slowActive)
                    {
                        g_slowStart = now;
                        g_slowActive = 1;
                        g_abortActive = 0;
                        InterlockedIncrement(&g_slowCount);
                        set_game_speed(SLOW_SPEED);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u >>> SLOW START (AR: ????????) %ums @0.10\n",
                                    (unsigned)now, (unsigned)g_slowDur);
                            fflush(g_slowF);
                        }
                        if (fastPlayer && strstr(g_mode, "6act") == NULL)
                        {
                            // ? ?? 250ms ??"????"?
                            //   ???????**?????**(???????), ???????
                            //   ???????????? -> ??"????"????????
                            //   ????????????, ???????????
                            float lenA = 0.0f;
                            int   aidA = -1;
                            unsigned long long pcA = ctr_of_player();
                            if (pcA != 0) aidA = anim_of_ctr(pcA, &lenA);
                            faPendingOpen = 1;
                            faOpenAt = now + 250;
                            faPendAnim = aidA;
                        }
                    }
                }
                // ?? v69: 6act ??"????????"??? ?? ??**???????????**
                //   ? / ?? / ???: ?????????, ?????????????,
                //   ?? v31 ??"??????"????????(?????+????)?
                //   ???? 0xB6A15A ?????? 17 ????/????????
                //   (????: 25 ?"????=??"?? 15 ????? = ????/????)?
                //   ??: ????=?? + 800ms ????? + ?????
                //   ???? 250ms ??? -> ??(??????????)
                if (!useArTrigger && strstr(g_mode, "6act") != NULL && isPlayer && !g_slowActive)
                {
                    DWORD dodgeAgo3 = (DWORD)(now - lastDodgePress);
                    int   fresh3 = (lastDodgePress != 0 && dodgeAgo3 <= 800) ? 1 : 0;
                    int   hpQuiet3 = (lastHpDrop == 0 || (DWORD)(now - lastHpDrop) > S6_HITQUIET_MS) ? 1 : 0;
                    // ★v75: 不管按没按闪避, 都先把"攻击方此刻的动画状态"记成命中状态。
                    //   格挡/弹反掉的箭也会走到这里 —— 这正是学远程最快的数据来源。
                    {
                        unsigned long long ac3 = ctr_of_attack_man(g_arA2);
                        if (ac3 != 0)
                        {
                            unsigned int vm3 = 0, vv3 = 0;
                            int vnpc3 = -1;
                            vm3 = model_of_ctr(ac3, &vv3, &vnpc3, now);
                            if (vm3 != 0)
                            {
                                unsigned long long aob3 = 0;
                                int   aAnim3 = -1;
                                float aT3 = 0.0f, pp3[3], ep3[3];
                                float d3 = -1.0f;
                                unsigned long long pc3b = ctr_of_player();
                                if (rd_ptr(ac3 + 0x10, &aob3) && aob3 != 0)
                                {
                                    rd_u32(aob3 + 0x20, &aAnim3);
                                    safe_read(aob3 + 0x24, &aT3, 4);
                                }
                                if (pc3b != 0 && ctr_pos(pc3b, pp3) && ctr_pos(ac3, ep3))
                                {
                                    float dx = ep3[0]-pp3[0], dy = ep3[1]-pp3[1], dz = ep3[2]-pp3[2];
                                    d3 = sqrtf(dx*dx + dy*dy + dz*dz);
                                }
                                if (aAnim3 >= 0)
                                {
                                    int acs3 = (int)(aT3 * 100.0f + 0.5f);
                                    rng_note(vm3, aAnim3, acs3, d3);
                                    if (g_slowF)
                                    {
                                        fprintf(g_slowF, "t=%u RNG 学到命中状态(结算) model=%u anim=%d cs=%d d=%.2f\n",
                                                (unsigned)now, vm3, aAnim3, acs3, d3);
                                        fflush(g_slowF);
                                    }
                                }
                            }
                        }
                    }
                    if (fresh3 && hpQuiet3)
                    {
                        arPending = 1;
                        arPendingAt = now;
                        arPendingHit = g_arHitSnap;
                        g_slowStart = now;
                        g_slowActive = 1;
                        g_abortActive = 1;
                        g_abortUntil = now + 250;
                        g_abortHit = (LONG)g_playerHitMs;
                        InterlockedIncrement(&g_slowCount);
                        set_game_speed(SLOW_SPEED);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u >>> SLOW START (?????????: ??/????, ??? %ums ?) %ums @%.2f arc=%ld\n",
                                    (unsigned)now, (unsigned)dodgeAgo3, (unsigned)g_slowDur,
                                    (double)SLOW_SPEED, an);
                            fflush(g_slowF);
                        }
                    }
                    else if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u ??-?-?-?? ????: freshDodge=%d(%ums) hpQuiet=%d\n",
                                (unsigned)now, fresh3, (unsigned)dodgeAgo3, hpQuiet3);
                        fflush(g_slowF);
                    }
                }
            }
            if (arPending)
            {
                if ((LONG)g_hitSeq != arPendingHit)
                {
                    arPending = 0;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u AR-CONFIRM: ????? -> %s\n", (unsigned)now,
                                g_slowActive ? "????" : "??????");
                        fflush(g_slowF);
                    }
                    if (g_slowActive)
                    {
                        g_slowActive = 0;
                        g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                    }
                }
                else if (now - arPendingAt >= 30)
                {
                    arPending = 0;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u AR-CONFIRM: ??? -> ??????\n", (unsigned)now);
                        fflush(g_slowF);
                    }
                }
            }
        }
        // 5-1) ? ????????(? 2ms)?
        //      ???"??? 40ms ???", ? 40ms ??????????????????,
        //      ???????? ?? v46 ? 23 ????????????
        //      ????????: ????, ??????????"??", ?????
        {
            int hpNow2 = -1;
            unsigned long long pch6 = player_chr();
            if (pch6 != 0) rd_u32(pch6 + 0x130, &hpNow2);
            if (hpNow2 >= 0)
            {
                if (lastPlayerHp >= 0 && hpNow2 < lastPlayerHp)
                {
                    // ★★ v100: 用户要求 —— 只要扣血(说明这次闪避没成功 / 被打断了),
                    //   缓速 + 特效 + 音效**立刻全部收掉**, 回到正常状态。
                    //   注意要放在"屏蔽伤害"之前: 屏蔽会把血量写回去, 那样就检测不到挨打了,
                    //   上一版就是这么漏掉"被砍中还继续缓速"的。
                    // ★★ v110: 缓速期间"只有真的挨了一刀(受击)才取消" —— 掉血也可能来自
                    //   中毒/持续伤/环境伤, 那种不算受击, 不该收掉缓速。
                    //   判据: 掉血前 250ms 内有一笔"攻击结算到我头上"的事件才算挨刀。
                    int isMeleeHit = (g_lastPlayerResMs != 0 && (DWORD)(now - g_lastPlayerResMs) <= 250) ? 1 : 0;
                    if (isMeleeHit) g_hurtLearnUntil = now + 250;   // ★v116: 之后 250ms 里学"受击动画"
                    if (g_slowActive && isMeleeHit)
                    {
                        g_slowActive = 0; g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u HIT-CANCEL: 挨刀扣血 %d->%d -> 缓速/特效/音效立刻收掉\n",
                                    (unsigned)now, lastPlayerHp, hpNow2);
                            fflush(g_slowF);
                        }
                    }
                else if (g_slowF && g_slowActive && !isMeleeHit)
                    {
                        fprintf(g_slowF, "t=%u 掉血 %d->%d 但不是挨刀(毒/持续/环境) -> 缓速保留\n",
                                (unsigned)now, lastPlayerHp, hpNow2);
                        fflush(g_slowF);
                    }
                    // ★★ v90: 屏蔽伤害 —— 在我们"成功闪避保护窗口"(触发后 1.6 秒)内,
                    //   只要检测到掉血, 立刻把血量写回去(等于这一下没打中你)。
                    //   为什么走这条路: 写动画时间能延长无敌(实测有效), 但覆盖面不够;
                    //   而"横扫刀法往刀路里闪还是被砍"本质是引擎已经判定了命中, 只有
                    //   把这次伤害抹掉才真正解决。
                    //   注意: 只有你确实按了闪避并触发过缓速的那 1.6 秒内才生效。
                    if (s6HoldUntil != 0 && (LONG)(s6HoldUntil - now) > 0 && g_playerHpAddr != 0)
                    {
                        int back = lastPlayerHp;
                        if (safe_write(g_playerHpAddr, &back, 4))
                        {
                            s6ShieldN++;
                            hpNow2 = back;      // 当作没掉血
                            if (g_slowF)
                            {
                                fprintf(g_slowF, "t=%u 屏蔽伤害: HP %d -> 已写回 %d (第 %d 次)\n",
                                        (unsigned)now, back, back, s6ShieldN);
                                fflush(g_slowF);
                            }
                        }
                    }
                }
                if (lastPlayerHp >= 0 && hpNow2 < lastPlayerHp)
                {
                    if (g_slowActive)
                    {
                        g_slowActive = 0;
                        g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u ABORT: ???? %d->%d -> ??????\n",
                                    (unsigned)now, lastPlayerHp, hpNow2);
                            fflush(g_slowF);
                        }
                    }
                    else if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u (?? %d->%d, ??????)\n",
                                (unsigned)now, lastPlayerHp, hpNow2);
                        fflush(g_slowF);
                    }
                    resPending = 0;
                }
                if (lastPlayerHp >= 0 && hpNow2 < lastPlayerHp) { g_hitSuppressUntil = now + 250; lastHpDrop = now; }
                // ★v107: 受击锁 —— 记一个明确的"不许起缓速"截止时间, 所有触发路都用它兜底
                lastPlayerHp = hpNow2;
            }
            else lastPlayerHp = -1;
        }

        if (fastPlayer)
        {
            // ???????: ???"???????", ????????????
            if (faPendingOpen && now >= faOpenAt)
            {
                float lenB = 0.0f;
                int   aidB = -1;
                unsigned long long pcB = ctr_of_player();
                faPendingOpen = 0;
                if (pcB != 0) aidB = anim_of_ctr(pcB, &lenB);
                if (aidB >= 0 && aidB == faPendAnim && !g_slowActive)
                {
                    faOn = 1; faAnim = aidB; faElen = lenB; faUntil = now + FAST_MS;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u ACTWINDOW open anim=%d elen=%.2f (AR+250ms)\n",
                                (unsigned)now, aidB, lenB);
                        fflush(g_slowF);
                    }
                }
                else if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u ACTWINDOW skipped (???? %d->%d)\n",
                            (unsigned)now, faPendAnim, aidB);
                    fflush(g_slowF);
                }
            }
            if (faOn)
            {
                float len4 = 0.0f;
                int   aid4 = -1;
                unsigned long long pc4 = ctr_of_player();
                if (pc4 != 0) aid4 = anim_of_ctr(pc4, &len4);
                int ended = (aid4 < 0) || (aid4 != faAnim) || (len4 < faElen - 0.05f);
                if (ended || now >= faUntil || !g_slowActive)
                {
                    faOn = 0;
                    faNeedRestore = 1;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u ACTWINDOW closed (%s) anim=%d elen=%.2f\n",
                                (unsigned)now,
                                !g_slowActive ? "??????" : (ended ? "???????" : "??"),
                                aid4, len4);
                        fflush(g_slowF);
                    }
                }
                else
                {
                    float f = (float)((g_clockSpeed > 0.05) ? (1.0 / g_clockSpeed) : 4.0);
                    set_player_playspeed(f);
                }
            }
            if (faNeedRestore)
            {
                set_player_playspeed(1.0f);
                faNeedRestore = 0;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u ACTWINDOW PlaySpeed ??? 1.00\n", (unsigned)now);
                    fflush(g_slowF);
                }
            }
        }

        // 5) ???????
        if (g_slowActive)
        {
            DWORD el = now - g_slowStart;

            if (fastPlayer)
            {
                /* ????????? if (g_slowActive) ???, ??? 5a) */
            }
            if (g_abortActive && now < g_abortUntil && (LONG)g_playerHitMs != g_abortHit)
            {
                g_slowActive = 0; g_abortActive = 0;
                set_game_speed(1.0);
                InterlockedIncrement(&g_abortCount);
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u <<< ABORT (was hit %ums after dodge)\n", (unsigned)now, (unsigned)el);
                    fflush(g_slowF);
                }
            }
            else
            {
                int acted = (el > 60 && newActionVk != 0) ? 1 : 0;
                // ★v98: 用户要的节奏 —— "下一步按下的那一瞬间, 缓速结束, 游戏回归正常"。
                //   所以"攻击/技能/再来一次闪避"都算"做出选择", 当场收掉缓速;
                //   什么都不做才会等满 1 秒(超时)。
                int chose = (el > 40 && (newActionVk != 0 || newDodge)) ? 1 : 0;
                if (g_abortActive && now >= g_abortUntil) g_abortActive = 0;
                if (chose && g_unlockOn)
                {
                    InterlockedIncrement(&g_unlockKeeps);
                    wLastKeyAt = now;
                    if (g_slowF && unlockKeyN == 0)
                    {
                        unlockKeyN = 1;
                        // ★★ v119: "技能取消连" —— 我在技能里按了闪避/技能 => 直接把当前技能动画
                        //   推到底, 让技能当场结束, 新动作(垫步/下一个技能)就能接上。
                        {
                            unsigned long long pcc = ctr_of_player(), obc = 0;
                            int aidc = -1;
                            float tc = 0.0f;
                            if (pcc != 0 && rd_ptr(pcc + 0x10, &obc) && obc != 0 &&
                                rd_u32(obc + 0x20, &aidc) && aidc >= 0 && safe_read(obc + 0x24, &tc, 4))
                            {
                                // ★v120: 放宽"能打断"的范围: 不是垫步、也不是受击动画, 且不是基础动作
                                //   (之前只认 79xxxx/5xxxxxxxx, 结果你说从头到尾没打断过 -> 说明
                                //    技能动画 id 不止那两段; 先把 id 记进日志, 再按数据收)
                                // ★★ v122: v120 放得太宽(anim>=100000 就打断) -> 一局里打断了 61 次,
                                //   连 101316001 这种都切了, 手感全毁。收回到**只认实测确认过的技能段**:
                                //   79xxxx(790010/790040/790060/790510...) 和 5xxxxxxxx。
                                int isSkill = (aidc >= 790000 && aidc < 800000) || (aidc >= 50000000 && aidc < 60000000);
                                if (g_slowF)
                                {
                                    fprintf(g_slowF, "t=%u KEYCUT? 按了键 anim=%d 判定=%s\n",
                                            (unsigned)now, aidc, isSkill ? "能打断" : "不动");
                                    fflush(g_slowF);
                                }
                                // ★v123: 打断只在 MODE 写 chain 时做; 默认只记日志(拿数据)
                                if (isSkill && tc < 3.0f && strstr(g_mode, "chain") != NULL)
                                {
                                    float t2c = tc + 1.5f;   // 推过技能尾部 -> 技能当场结束
                                    // ★v120: 写一次会被引擎下一帧盖回去 —— 记下"持续压 200ms"
                                    g_skillCutUntil = now + 200;
                                    g_skillCutAnim = aidc;
                                    if (safe_write(obc + 0x24, &t2c, 4) && g_slowF)
                                    {
                                        fprintf(g_slowF, "t=%u SKILL-CUT: 技能 %d 被打断(animT %.2f -> %.2f)\n",
                                                (unsigned)now, aidc, (double)tc, (double)t2c);
                                        fflush(g_slowF);
                                    }
                                }
                            }
                        }
                        // ★v105: 可选开关 invrel —— 玩家做出选择的瞬间, 连"强制无敌"也一起撤掉
                        //   (默认保持原样: 触发后 2.2 秒内无论你在做什么, 打到你的攻击都被吃掉)
                        if (strstr(g_mode, "invrel") != NULL) g_invForceUntil = 0;
                        fprintf(g_slowF, "t=%u UNLOCK-KEY 玩家做出选择(vk=0x%X%s) -> 缓速当场结束, el=%ums\n",
                                (unsigned)now, newActionVk, newDodge ? "/闪避" : "", (unsigned)el);
                        fflush(g_slowF);
                        // ★v95 试验开关: MODE 里写 cut -> 你出手的那一下, 直接把当前动作
                        //   (垫步)的动画时间推过去, 让它立刻结束, 动作锁定当场解除。
                        //   默认不开; 只在"取消倍速后动作还是出不来"时用来对比。
                        if (strstr(g_mode, "cut") != NULL)
                        {
                            unsigned long long pcq = ctr_of_player(), obq = 0;
                            float tq = 0.0f;
                            if (pcq != 0 && rd_ptr(pcq + 0x10, &obq) && obq != 0 &&
                                safe_read(obq + 0x24, &tq, 4))
                            {
                                float t2 = tq + 1.0f;
                                if (safe_write(obq + 0x24, &t2, 4) && g_slowF)
                                {
                                    fprintf(g_slowF, "t=%u CUT 强行走完当前动作: 动画时间 %.3f -> %.3f\n",
                                            (unsigned)now, tq, t2);
                                    fflush(g_slowF);
                                }
                            }
                        }
                    }
                    acted = 1;      // 走"结束缓速"那条路
                }
                if (el >= g_slowDurNow || acted)
                {
                    g_slowActive = 0; g_abortActive = 0;
                    set_game_speed(1.0);
                    // ★v118: 这一窗结束, 参数回归标准(下一个窗口重新判定)
                    g_slowSpeedNow = SLOW_SPEED;
                    g_slowDurNow   = SLOW_MS;
                    // ★★ v106: 用户规则 —— **只有缓速正常结束**(超时, 或你按下了下一步)才给
                    //   0.25 秒无敌, 专门用来吃那些垫步躲不掉的攻击。触发瞬间、受击取消、
                    //   空中作废这些路一律不给。
                    g_invForceUntil = (LONG)(now + 250);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u <<< SLOW END %s el=%ums\n", (unsigned)now,
                                acted ? "(player acted)" : "(timeout)", (unsigned)el);
                        fflush(g_slowF);
                    }
                }
                else
                {
                    DWORD d = g_slowDurNow, r = d / 10;
                    double sp = (double)g_slowSpeedNow;
                    if (el < r)           set_game_speed(1.0 + (sp - 1.0) * ((double)el / (double)r));
                    else if (el < d - r)  set_game_speed(sp);
                    else                  set_game_speed(sp + (1.0 - sp) * ((double)(el - (d - r)) / (double)r));
                }
            }
        }

        // 6) ???? -> ????, ?????? (??? 200ms ??)
        if (newDodge)
        {
            lastDodgePress = now;
            press_snapshot(now);          // ★v104: 记下"按闪避那一刻"各敌人的距离
            // ★v112: 顺手记下"这一下要躲的那个敌人"(离我最近的那个)
            {
                int bi = -1, b2;
                float bd = 1e9f;
                for (b2 = 0; b2 < 6; b2++)
                    if (g_pressCtr[b2] != 0 && g_pressD[b2] > 0.0f && g_pressD[b2] < bd)
                    { bd = g_pressD[b2]; bi = b2; }
                if (bi >= 0 && bd <= 6.0f)
                {
                    // ★v114: "刚挨过打"的时候按闪避不算(受击硬直里按闪避, 动作根本出不来)
                    if (lastHpDrop != 0 && (DWORD)(now - lastHpDrop) <= S6_HITQUIET_MS)
                    {
                        g_armCtr = 0;
                        if (g_slowF) { fprintf(g_slowF, "t=%u ARM 不记(刚挨过打 %ums)\n",
                                               (unsigned)now, (unsigned)(now - lastHpDrop)); fflush(g_slowF); }
                    }
                    else
                    {
                    g_armCtr = g_pressCtr[bi];
                    g_armAt = now;
                    g_armHitSnap = (LONG)g_playerHitMs;
                    g_armSawDodge = 0;
                    // ★★ v129: 用户"音效给慢了" —— 按闪避那一瞬就该有声音。
                    //   条件: 这个敌人现在就在出招(认得出来的判定窗口附近) -> 立刻放提示音。
                    {
                        unsigned long long obx = 0;
                        unsigned int mx = 0, vx = 0;
                        int npcx = -1, animx = -1, csx = -1, wsx = 0, wex = 0;
                        float tx = 0.0f;
                        mx = model_of_ctr(g_armCtr, &vx, &npcx, now);
                        if (mx != 0 && rd_ptr(g_armCtr + 0x10, &obx) && obx != 0 &&
                            rd_u32(obx + 0x20, &animx) && animx >= 0 && safe_read(obx + 0x24, &tx, 4))
                        {
                            csx = (int)(tx * 100.0f + 0.5f);
                            if (atk_window(mx, vx, animx, &wsx, &wex) &&
                                csx >= wsx - 60 && csx <= wex + 15)
                            {
                                fx_sting();
                                g_stingPlayedAt = now;
                                if (g_slowF) { fprintf(g_slowF, "t=%u STING-EARLY 按闪避时就放音(敌 anim=%d cs=%d 窗口=%d-%d)\n",
                                                       (unsigned)now, animx, csx, wsx, wex); fflush(g_slowF); }
                            }
                        }
                    }
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u ARM 记下躲闪目标 ctr=%llX d=%.2f\n",
                                (unsigned)now, g_armCtr, (double)bd);
                        fflush(g_slowF);
                    }
                    }
                }
                else g_armCtr = 0;
            }
            if (g_slowF)
            {
                // ???????????? ?? ????"???"???????
                pressPendingAccept = 1;
                lastPressAt = now;
                {
                    unsigned long long pc3 = ctr_of_player();
                    float len3 = 0.0f;
                    int aid3 = (pc3 != 0) ? anim_of_ctr(pc3, &len3) : -1;
                    int act3 = -1;
                    {
                        unsigned long long ch3 = player_chr();
                        if (ch3 != 0) safe_read(ch3 + 0x230, &act3, 4);
                    }
                    fprintf(g_slowF, "t=%u PRESS dodge anim=%d elen=%.3f act=%d\n",
                            (unsigned)now, aid3, len3, act3);
                    fflush(g_slowF);
                    // ?????????"????????" ?? ????
                    // "???" ? "?????", ??????????????
                    {
                        float dist3 = -1.0f;
                        unsigned long long pctr3 = ctr_of_player(), ectr3 = enemy_ctr_best();
                        float a3[3], b3[3];
                        if (pctr3 != 0 && ectr3 != 0 && ctr_pos(pctr3, a3) && ctr_pos(ectr3, b3))
                        {
                            float dx = b3[0]-a3[0], dy = b3[1]-a3[1], dz = b3[2]-a3[2];
                            dist3 = sqrtf(dx*dx + dy*dy + dz*dz);
                        }
                        fprintf(g_slowF, "t=%u PRESS dist=%.2f\n", (unsigned)now, dist3);
                        fflush(g_slowF);
                    }
                }
                fp_dump_player("PRESS", now, 0);
            }
        }
        if (pressPendingAccept && (now - lastPressAt) > 320)
        {
            pressPendingAccept = 0;
            if (g_slowF)
            {
                fprintf(g_slowF, "t=%u PRESS-IGNORED (320ms ???????)\n", (unsigned)now);
                fflush(g_slowF);
            }
        }

        // 6-1) ?? v67: ???? v31 ???(v66 ? A/B ??????)
        //   v31 = "?????????":
        //     ? ???? (dist < 2.5)
        //     ? ??????????????????(????? / ???????)
        //     ? ????(400ms ?)?? ?? ???????, ??????
        //     ? ???? 400ms ??? -> ???? (v31 ? abort ??)
        //   **????????** ?? v65/v66 ??"?????? [start,end] ???",
        //   ???????"??? / ????"????
        //   ????? + ??????? v66 ?????(?????"?????", ?????)?
        // ★★★ v112: 回溯判定 —— "起手认不出招" 的兜底(见文件上方 g_armCtr 的说明)
        if (g_armCtr != 0)
        {
            if (g_slowActive) g_armCtr = 0;                       // 已经有窗口了, 不用这条
            else if ((LONG)g_playerHitMs != g_armHitSnap)         // 这期间挨打了 -> 这次闪避不算成功
            {
                if (g_slowF) { fprintf(g_slowF, "t=%u ARM 作废(这期间受击)\n", (unsigned)now); fflush(g_slowF); }
                g_armCtr = 0;
            }
            else if ((DWORD)(now - g_armAt) > 700)                // 超时
            {
                if (g_slowF) { fprintf(g_slowF, "t=%u ARM 超时(700ms 内没等到判定窗口)\n", (unsigned)now); fflush(g_slowF); }
                g_armCtr = 0;
            }
            else
            {
                // ★v114: 先看"这次垫步到底有没有出来" —— 只有玩家真的进了带无敌帧的动作,
                //   这一下才算闪避(受击硬直里按闪避、动作没出来 -> 永远不满足, 不会给缓速)
                {
                    unsigned long long pcb = ctr_of_player(), obb = 0;
                    int aidb = -1;
                    float tb = 0.0f;
                    if (pcb != 0 && rd_ptr(pcb + 0x10, &obb) && obb != 0 &&
                        rd_u32(obb + 0x20, &aidb) && aidb >= 0 && safe_read(obb + 0x24, &tb, 4))
                    {
                        unsigned short sb = 0, eb = 0;
                        int csb = (int)(tb * 100.0f + 0.5f);
                        if (ifr_find(aidb, &sb, &eb) && csb >= (int)sb && csb <= (int)eb)
                            g_armSawDodge = 1;
                    }
                }
                unsigned long long ob2 = 0, pc2 = ctr_of_player();
                unsigned int m2 = 0, var2 = 0;
                int npc2 = -1, anim2 = -1, cs2 = -1, ws2 = 0, we2 = 0, haveWin = 0;
                float t2 = 0.0f, d2 = -1.0f;
                m2 = model_of_ctr(g_armCtr, &var2, &npc2, now);
                if (m2 != 0 && rd_ptr(g_armCtr + 0x10, &ob2) && ob2 != 0 &&
                    rd_u32(ob2 + 0x20, &anim2) && anim2 >= 0 && safe_read(ob2 + 0x24, &t2, 4))
                {
                    cs2 = (int)(t2 * 100.0f + 0.5f);
                    if (pc2 != 0)
                    {
                        float a2[3], b2[3];
                        if (ctr_pos(pc2, a2) && ctr_pos(g_armCtr, b2))
                        {
                            float dx = b2[0]-a2[0], dy = b2[1]-a2[1], dz = b2[2]-a2[2];
                            d2 = sqrtf(dx*dx + dy*dy + dz*dz);
                        }
                    }
                    if (atk_window(m2, var2, anim2, &ws2, &we2)) haveWin = 1;
                    else
                    {
                        unsigned short s3 = 0, e3 = 0; short j3 = 0;
                        if (ev_find(m2, anim2, 1, cs2, &s3, &e3, &j3)) { ws2 = (int)s3; we2 = (int)e3; haveWin = 1; }
                    }
                    if (haveWin && cs2 >= ws2 - S6_LEAD_CS && cs2 <= we2 + S6_TAIL_CS &&
                        (d2 < 0.0f || d2 <= reach_gate2(m2, anim2, 0)) && g_armSawDodge)
                    {
                        // 就是这一下了: 判定窗口打开 + 我在射程内 + 这期间没受伤 = 成功闪避
                        g_slowStart = now; g_slowActive = 1; g_abortActive = 0;
                        InterlockedIncrement(&g_slowCount);
                        InterlockedIncrement(&g_armN);
                        set_game_speed(SLOW_SPEED);
                        s6LastCtr = g_armCtr; s6LastAnim = anim2; s6LastWin = ws2; s6LastCs = cs2;
                        s6HoldUntil = inv_hold_on_trigger(now);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u >>> SLOW START (v112 回溯判定: 起手认不出、判定帧命中) anim=%d cs=%d win=%d.%02d-%d.%02d d=%.2f)\n",
                                    (unsigned)now, anim2, cs2, ws2 / 100, (ws2 < 0 ? 0 : ws2 % 100),
                                    we2 / 100, (we2 < 0 ? 0 : we2 % 100), (double)d2);
                            fflush(g_slowF);
                        }
                        g_armCtr = 0;
                    }
                }
            }
        }

        if (strstr(g_mode, "6act") != NULL)
        {
            double tb0 = qms();
            S6Cand cd[S6_CAND_MAX];
            int    ncd = 0, hi = -1, k;
            DWORD  ago   = (DWORD)(now - lastDodgePress);
            int    forceFar = (strstr(g_mode, "far") != NULL) ? 1 : 0;
            int    noAir    = (strstr(g_mode, "noair") != NULL) ? 1 : 0;
            int    anyAtk;
            s6_refresh_near(now);
            anyAtk = s6_scan(cd, S6_CAND_MAX, now, &s6LastAtk, &s6LastAtkT,
                             g_lastHitCtr, forceFar, &ncd);
            int    hpQuiet = (lastHpDrop == 0 || (DWORD)(now - lastHpDrop) > S6_HITQUIET_MS) ? 1 : 0;
            for (k = 0; k < ncd; k++)
                if (cd[k].isAtk != 0 && (hi < 0 || cd[k].dist < cd[hi].dist)) hi = k;
            // ?v70: ???"??????", ??? 1.5 ?????????????
            //       -> ???/??/??????
            // v74: 换成"学到的远程命中点"匹配。旧的是"1.5 秒内出过招",
            //      太粗 —— 玩家自己突刺、识破的时候都会误触发, v73 的手感就是这么坏的。
            int graceHitAnim = -1, graceHitCs = -1;
            float graceHitDist = -1.0f;
            unsigned int graceHitModel = 0;
            // ★v84: 记住这次匹配到的"窗口起点", 用来判断"是不是同一刀又按了一次"
            int graceWinStart = 0;
            unsigned long long graceHitCtr = 0;
            int winHit = 0;     // 1=表窗口 2=学到的命中状态
            if (!noAir)
                for (k = 0; k < ncd; k++)
                {
                    if (cd[k].dist >= 0.0f && cd[k].dist > S6_GRACE_MAXD) continue;
                    // B) 我们学到的"命中状态"(近战/箭/苦无通用, 不分远近)
                    // ★v86: 还要求"当前距离和我们真正观测到过的那次距离接近" ——
                    //   否则近战学到的一个点会在大老远被外推命中(飞渡浮舟那次就是这样)。
                    if (cd[k].dist >= S6_GRACE_DIST && rng_match(cd[k].model, cd[k].anim, cd[k].cs, cd[k].dist))
                    {
                        graceHitAnim = cd[k].anim; graceHitCs = cd[k].cs;
                        graceHitDist = cd[k].dist; graceHitModel = cd[k].model;
                        winHit = 2;
                        graceWinStart = 0; graceHitCtr = cd[k].ctr;
                        break;
                    }
                    // A) 社区表的判定窗口, 提前量给足(玩家通常比命中早按)
                    if (cd[k].isAtk == 1 && !cd[k].oor &&
                        cd[k].cs >= cd[k].ws - 90 && cd[k].cs <= cd[k].we + 25)
                    {
                        graceHitAnim = cd[k].anim; graceHitCs = cd[k].cs;
                        graceHitDist = cd[k].dist; graceHitModel = cd[k].model;
                        winHit = 1;
                        graceWinStart = (int)cd[k].ws; graceHitCtr = cd[k].ctr;
                        break;
                    }
                    // ★v78-A) 我们自己从档案里解出来的"攻击判定帧"(比社区表准):
                    //   命中帧在 [start,end], 玩家会比它早按 -> 提前量 0.35 秒
                    {
                        unsigned short es = 0, ee = 0; short ej = 0;
                        // ★v86: 这条是"近战"通道, 必须要够得着(!oor) —— 之前漏了这个条件,
                        //   于是"飞渡浮舟"那种长连招动画, 玩家在远处空放闪避也会触发。
                        if (!cd[k].oor &&
                            ev_find(cd[k].model, cd[k].anim, 1, cd[k].cs, &es, &ee, &ej) &&
                            cd[k].cs >= (int)es - 90 && cd[k].cs <= (int)ee + 25)
                        {
                            graceHitAnim = cd[k].anim; graceHitCs = cd[k].cs;
                            graceHitDist = cd[k].dist; graceHitModel = cd[k].model;
                            winHit = 3;
                            graceWinStart = (int)es; graceHitCtr = cd[k].ctr;
                            break;
                        }
                    }
                    // ★v78-B) 放弹丸(箭/苦无/手里剑): 事件时刻 = 离手时刻,
                    //   之后弹丸在飞 -> 这段时间里按闪避都算“躲这一发”(窗口按距离放大)
                    {
                        unsigned short es = 0, ee = 0; short ej = 0;
                        // ★回到 v81 的算法(用户明确要求"飞箭判定就按 81 的来")
                        int flight = 60 + (int)(cd[k].dist * 12.0f);   // 0.6s + 每米 0.12s
                        if (flight > 200) flight = 200;
                        // ★v80: judge 980/981 = 社区文档里明确的"零伤害危字提示载体/纯特效",
                        //   不是真弹丸(弦一郎的突刺 3025 就带一个 980)。不排除的话,
                        //   他的突刺会被当成远程走"按下瞬间", 破坏识破手感。
                        // v81: 从整段引弓到"离手 + 飞行"都算(只排除 judge 980/981 的假弹丸)
                        if (ev_find(cd[k].model, cd[k].anim, 2, cd[k].cs, &es, &ee, &ej) &&
                            ej != 980 && ej != 981 &&
                            cd[k].cs >= -10 && cd[k].cs <= (int)ee + flight)
                        {
                            graceHitAnim = cd[k].anim; graceHitCs = cd[k].cs;
                            graceHitDist = cd[k].dist; graceHitModel = cd[k].model;
                            winHit = 4;
                            graceWinStart = (int)es; graceHitCtr = cd[k].ctr;
                            break;
                        }
                    }
                }
            anyAtk = (winHit > 0) ? 1 : 0;

            // (v82 那条"150ms 动画没变就撤销"的判据已删除 —— 实测它会误杀真闪避:
            //  你已经在垫步动画里再按一次、或者跑步接垫步时, 动画本来就不会变,
            //  结果 23 次缓速被撤销 11 次。用户反馈"近战闪避一直不出缓速"就是这个。)
            // ★v76: 默认不再"按下即缓速" —— 按下只算"待定", 等确认:
            //   ① 这一击被无敌帧/格挡吃掉(结算事件: 攻击打到我头上但没掉血) -> 立刻缓速 (= 真正躲开的那一瞬间)
            //   ② 先掉血了 -> 说明这次闪避没成功, 干脆不缓速(修掉"缓速了还是挨打")
            //   ③ 450ms 内既没掉血也没有结算事件 -> 那一刀根本够不着(或已过), 也算躲开, 补触发
            //   MODE 里带 fast 就恢复成"按下瞬间立刻缓速"的老手感。
            int fastMode = (strstr(g_mode, "fast") != NULL) ? 1 : 0;
            // ★v77: 这一下是不是"识破突刺危"? 是的话走 v76 的确认式; 其它一律"按下瞬间"(v72 手感)
            int mikiriMode = 0;
            for (k = 0; k < ncd; k++)
                if (cd[k].anim >= 0 && anim_is_mikiri(cd[k].model, cd[k].anim)) { mikiriMode = 1; break; }
            if (newDodge && !g_slowActive && hpQuiet && (anyAtk || graceHitAnim >= 0))
            {
                const S6Cand* c = (hi >= 0) ? &cd[hi] : NULL;
                // ★v90: 刚格挡掉的攻击之后的闪避按一下, 不是"躲开那一下" —— 不触发。
                //   (你用格挡挡下飞箭, 紧接着按闪避也缓速, 就是这个原因。)
                if (s6BlockAt != 0)
                {
                    // ★v91: 用户反馈"400ms 太短"。格挡掉的如果是飞箭(远程通道),
                    //        之后再按闪避 1.5 秒内都不算数; 近战仍是 400ms。
                    DWORD bago = (DWORD)(now - s6BlockAt);
                    int   rangedChan = (winHit == 2 || winHit == 4) ? 1 : 0;
                    DWORD lim = rangedChan ? 1500 : 400;
                    if (bago <= lim)
                    {
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u 格挡后 %ums 内按闪避(通道=%s) -> 不缓速\n",
                                    (unsigned)now, (unsigned)bago, rangedChan ? "远程" : "近战");
                            fflush(g_slowF);
                        }
                        goto s6_trigger_done;
                    }
                }
                // ★v84: 同一刀只缓速一次 —— 判断依据: 同一个敌人容器 + 同一个动画 +
                //   同一个窗口起点, 而且动画时间还没倒回去(倒回去说明是新的一刀)。
                //   第一段缓速结束(1 秒到 / 你出招打断)之后再按闪避, 就不会重复触发了。
                if (graceHitCtr != 0 && graceHitCtr == s6LastCtr &&
                    graceHitAnim == s6LastAnim && graceWinStart == s6LastWin &&
                    graceHitCs >= s6LastCs)
                {
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u 同一刀又按了一次(anim=%d 窗口@%d cs=%d) -> 不重复缓速\n",
                                (unsigned)now, graceHitAnim, graceWinStart, graceHitCs);
                        fflush(g_slowF);
                    }
                    goto s6_trigger_done;
                }
                if (mikiriMode && !fastMode)
                {
                    s6Arm = 1; s6ArmAt = now; s6ArmHit = (LONG)g_hitSeq;
                    s6ArmAny = anyAtk; s6ArmAnim = anyAtk ? (c ? c->anim : -1) : graceHitAnim;
                    s6ArmCs = anyAtk ? (c ? c->cs : -1) : graceHitCs;
                    s6ArmModel = anyAtk ? (c ? c->model : 0) : graceHitModel;
                    s6ArmCtr = graceHitCtr; s6ArmWin = graceWinStart;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u (按下闪避, 待确认: %s anim=%d t=%d.%02ds)\n",
                                (unsigned)now, anyAtk ? ((winHit == 2) ? "学到的命中状态" : ((winHit == 3) ? "档案判定帧" : ((winHit == 4) ? "档案放弹丸" : "社区表窗口"))) : "远程命中状态",
                                s6ArmAnim, s6ArmCs / 100, (s6ArmCs < 0 ? 0 : s6ArmCs % 100));
                        fflush(g_slowF);
                    }
                    goto s6_arm_confirm_jump;
                }
                g_slowStart = now;
                g_slowActive = 1;
                // ? v31 ?????: ???? 400ms ??? -> ????????, ??
                g_abortActive = 1;
                g_abortUntil = now + S6_ABORT_MS;
                g_abortHit = (LONG)g_playerHitMs;
                InterlockedIncrement(&g_slowCount);
                set_game_speed(SLOW_SPEED);
                s6LastCtr = graceHitCtr; s6LastAnim = graceHitAnim; s6LastWin = graceWinStart; s6LastCs = graceHitCs;
                s6HoldUntil = inv_hold_on_trigger(now);   // ★v106: 默认不再钉住/不再给无敌
                if (g_slowF)
                {
                    if (anyAtk)
                        fprintf(g_slowF, "t=%u >>> SLOW START (v79-%s: anim=%d t=%d.%02ds %s d=%.2f) %ums @%.2f abandon=%dms\n",
                                (unsigned)now,
                                (winHit == 2) ? "学到的命中状态" : ((winHit == 3) ? "档案判定帧" : ((winHit == 4) ? "档案放弹丸" : "社区表窗口")),
                                graceHitAnim, graceHitCs / 100, (graceHitCs < 0 ? 0 : graceHitCs % 100),
                                (winHit == 3 || winHit == 4) ? "(我们档案里的事件)" : "(社区表窗口)",
                                graceHitDist,
                                (unsigned)g_slowDur, (double)SLOW_SPEED, S6_ABORT_MS);
                    else
                        fprintf(g_slowF, "t=%u >>> SLOW START (v75 学到的命中状态: model=%u anim=%d t=%d.%02ds d=%.2f) %ums @%.2f\n",
                                (unsigned)now, graceHitModel, graceHitAnim,
                                graceHitCs / 100, (graceHitCs < 0 ? 0 : graceHitCs % 100),
                                graceHitDist,
                                (unsigned)g_slowDur, (double)SLOW_SPEED);
                    fflush(g_slowF);
                }
            }
            else if (g_slowF && newDodge && (!anyAtk || !hpQuiet))
            {
                fprintf(g_slowF, "t=%u 6act-SKIP ????????: anyAtk=%d hpQuiet=%d (?? %ums ?) n=%d",
                        (unsigned)now, anyAtk, hpQuiet,
                        lastHpDrop ? (unsigned)(now - lastHpDrop) : 0, ncd);
                for (k = 0; k < ncd; k++)
                    fprintf(g_slowF, " [m=%u anim=%d d=%.2f atk=%d gate=%.1f inWin=%d oor=%d cs=%d win=%d.%02d-%d.%02d]",
                            cd[k].model, cd[k].anim, cd[k].dist, cd[k].isAtk,
                            (double)reach_gate2(cd[k].model, cd[k].anim, forceFar),
                            cd[k].inWin, cd[k].oor, cd[k].cs,
                            cd[k].ws / 100, (cd[k].ws < 0 ? 0 : cd[k].ws % 100),
                            cd[k].we / 100, (cd[k].we < 0 ? 0 : cd[k].we % 100));
                fputc('\n', g_slowF);
                fflush(g_slowF);
            }
        s6_trigger_done:
        s6_arm_confirm_jump:
            // (v84: 同一刀重复按下的分支 goto 到这里, 直接跳过"起缓速"那段)

            // ★★ v86: "钉住无敌帧" —— 只在缓速期间生效, 且必须 MODE 里带 if。
            //   原理: 玩家的无敌是由动画里的 IFrames 事件驱动的(垫步 213302 只有 0~0.20 秒)。
            //         缓速期间把当前动画时间压在无敌窗口末尾之前, 无敌就一直有效 ——
            //         于是"横扫刀法往刀路里闪也被砍中"这种情况能被吃掉。
            //   代价: 垫步动画会在这段时间"悬停", 看起来像滑步。
            // ★v88: 钉住的时间从"缓速那 1 秒"延长到"触发后 1.6 秒"。
            //   原因: 缓速 1 秒 = 游戏时间只有 0.25 秒, 无敌只从 0.20 延到约 0.45 秒;
            //   横扫刀的命中帧可能落在更后面, 所以还是要被砍。
            //   (钉住只在"你正处于垫步无敌窗口内"时才生效, 其它时刻不动你的动画。)
            if (s6HoldUntil != 0 && (LONG)(s6HoldUntil - now) > 0 && strstr(g_mode, "if") != NULL)
            {
                // ★★ v94: "钉住无敌帧" 和 "突破限制" 是互相打架的:
                //   钉住 = 把垫步动画时间压回无敌窗口末尾 -> 动画永远走不完 ->
                //   玩家卡在垫步动作里, 攻击/技能全被吃掉(用户报的"人直接立正了")。
                //   所以: 窗口内只要玩家一按键(想出手), 立刻松开钉住, 让动画走完。
                if (g_unlockOn && unlockKeyN)
                {
                    if (g_slowF && !s6PinOffLogged)
                    {
                        s6PinOffLogged = 1;
                        fprintf(g_slowF, "t=%u 钉住无敌帧: 松开(玩家在窗口内按键了, 让他出招)\n",
                                (unsigned)now);
                        fflush(g_slowF);
                    }
                }
                else
                {
                unsigned long long pc9 = ctr_of_player(), ob9 = 0;
                int   aid9 = -1;
                float t9 = 0.0f;
                if (pc9 != 0 && rd_ptr(pc9 + 0x10, &ob9) && ob9 != 0)
                {
                    rd_u32(ob9 + 0x20, &aid9);
                    if (aid9 >= 0 && safe_read(ob9 + 0x24, &t9, 4))
                    {
                        unsigned short s9 = 0, e9 = 0;
                        int cs9 = (int)(t9 * 100.0f + 0.5f);
                        // ★v89: 不只是"还在无敌窗口里"才钉 —— 已经过期但还在这个垫步动作里
                        //   (cs 比窗口末尾多 0.9 秒以内)也要把时间写回去, 让无敌重新生效。
                        //   这样"缓速起得晚了 / 先按了一次"的情况也覆盖得到。
                        if (ifr_find(aid9, &s9, &e9) && cs9 >= (int)s9 && cs9 <= (int)e9 + 90)
                        {
                            float hold = (float)(((int)e9 > 3) ? ((int)e9 - 3) : 0) / 100.0f;
                            if (t9 > hold && safe_write(ob9 + 0x24, &hold, 4))
                            {
                                static DWORD lastIfLog = 0, ifCount = 0;
                                if (g_slowF && (now - lastIfLog) > 300)
                                {
                                    lastIfLog = now;
                                    ifCount++;
                                    fprintf(g_slowF, "t=%u 钉住无敌帧: 动画=%d 无敌窗口 %d.%02d~%d.%02ds (第 %u 次)\n",
                                            (unsigned)now, aid9,
                                            s9 / 100, s9 % 100, e9 / 100, e9 % 100, (unsigned)ifCount);
                                    fflush(g_slowF);
                                }
                            }
                        }
                    }
                }
                }
            }
            // ---- 待确认状态机(v76 的核心) ----
            if (s6Arm && !g_slowActive)
            {
                int   damaged = ((LONG)g_hitSeq != s6ArmHit) ? 1 : 0;
                DWORD age = (DWORD)(now - s6ArmAt);
                if (damaged)
                {
                    s6Arm = 0;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u 闪避未成: 按了闪避但挨打(%dms 内) -> 不缓速 [%s anim=%d t=%d.%02ds]\n",
                                (unsigned)now, (int)age, s6ArmAny ? "近战" : "远程",
                                s6ArmAnim, s6ArmCs / 100, (s6ArmCs < 0 ? 0 : s6ArmCs % 100));
                        fflush(g_slowF);
                    }
                }
                // ★v90: 识破要"慢一点" —— 确认事件到达后也要等满 150ms 才起缓速。
                //   以前是"这一击被吃掉"的事件一到就立刻缓速(最快几十毫秒),
                //   所以感觉变成"闪避一开始就缓速", 不是你要的那种手感。
                else if (g_negAt != 0 && (LONG)(g_negAt - s6ArmAt) >= 0 && age >= 150)
                {
                    // 这一击被吃掉了 = 真正躲开的那一瞬间
                    s6Arm = 0;
                    s6LastCtr = s6ArmCtr; s6LastAnim = s6ArmAnim; s6LastWin = s6ArmWin; s6LastCs = s6ArmCs;
                    s6HoldUntil = inv_hold_on_trigger(now);   // ★v106
                    g_slowStart = now;
                    g_slowActive = 1;
                    g_abortActive = 1;
                    g_abortUntil = now + S6_ABORT_MS;
                    g_abortHit = (LONG)g_playerHitMs;
                    InterlockedIncrement(&g_slowCount);
                    set_game_speed(SLOW_SPEED);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u >>> SLOW START (v76-确认闪避: 攻击被吃掉, 按闪避后 %ums) [%s anim=%d t=%d.%02ds]\n",
                                (unsigned)now, (int)age, s6ArmAny ? "近战" : "远程",
                                s6ArmAnim, s6ArmCs / 100, (s6ArmCs < 0 ? 0 : s6ArmCs % 100));
                        fflush(g_slowF);
                    }
                }
                else if (age > 450)
                {
                    // 既没掉血也没结算事件: 这一刀没够着 -> 也算躲开
                    s6Arm = 0;
                    s6LastCtr = s6ArmCtr; s6LastAnim = s6ArmAnim; s6LastWin = s6ArmWin; s6LastCs = s6ArmCs;
                    s6HoldUntil = inv_hold_on_trigger(now);   // ★v106
                    g_slowStart = now;
                    g_slowActive = 1;
                    g_abortActive = 1;
                    g_abortUntil = now + S6_ABORT_MS;
                    g_abortHit = (LONG)g_playerHitMs;
                    InterlockedIncrement(&g_slowCount);
                    set_game_speed(SLOW_SPEED);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u >>> SLOW START (v76-超时补: 450ms 内没掉血也没结算) [%s anim=%d t=%d.%02ds]\n",
                                (unsigned)now, s6ArmAny ? "近战" : "远程",
                                s6ArmAnim, s6ArmCs / 100, (s6ArmCs < 0 ? 0 : s6ArmCs % 100));
                        fflush(g_slowF);
                    }
                }
            }
            // ?? ?/? ???: ????, ???
            if (g_slowF && (now - s6WinLog) > 40 && anyAtk != s6WasIn)
            {
                s6WinLog = now;
                fprintf(g_slowF, "t=%u 6act-EDGE %s", (unsigned)now, anyAtk ? "OPEN" : "close");
                for (k = 0; k < ncd; k++)
                    fprintf(g_slowF, " [m=%u anim=%d t=%d.%02d win=%d.%02d-%d.%02d d=%.2f atk=%d w=%d g=%.1f]",
                            cd[k].model, cd[k].anim,
                            cd[k].cs / 100, (cd[k].cs < 0 ? 0 : cd[k].cs % 100),
                            cd[k].ws / 100, (cd[k].ws < 0 ? 0 : cd[k].ws % 100),
                            cd[k].we / 100, (cd[k].we < 0 ? 0 : cd[k].we % 100),
                            cd[k].dist, cd[k].isAtk, cd[k].inWin,
                            (double)reach_gate2(cd[k].model, cd[k].anim, forceFar));
                fprintf(g_slowF, " hpQuiet=%d dodgeAgo=%ums\n", hpQuiet, (unsigned)ago);
                fflush(g_slowF);
            }
            // ????: ? 40ms ??, ???????"??????????"
            {
                int candNear = 0;   // ??: "near" ? Windows ????????
                for (k = 0; k < ncd; k++)
                    if (cd[k].dist < 0.0f || cd[k].dist < 10.0f) candNear = 1;
                if (g_slowF && !g_slowActive && candNear && (now - s6Trace) > 40 && ncd > 0)
                {
                    s6Trace = now;
                fprintf(g_slowF, "t=%u TR dodgeAgo=%ums hpAgo=%ums n=%d", (unsigned)now, (unsigned)ago,
                        lastHpDrop ? (unsigned)(now - lastHpDrop) : 0, ncd);
                for (k = 0; k < ncd; k++)
                    fprintf(g_slowF, " [m=%u anim=%d t=%d.%02d win=%d.%02d-%d.%02d d=%.2f atk=%d w=%d g=%.1f%s]",
                            cd[k].model, cd[k].anim,
                            cd[k].cs / 100, (cd[k].cs < 0 ? 0 : cd[k].cs % 100),
                            cd[k].ws / 100, (cd[k].ws < 0 ? 0 : cd[k].ws % 100),
                            cd[k].we / 100, (cd[k].we < 0 ? 0 : cd[k].we % 100),
                            cd[k].dist, cd[k].isAtk, cd[k].inWin,
                            (double)reach_gate2(cd[k].model, cd[k].anim, forceFar), (k == 0) ? "(tgt)" : "");
                fputc('\n', g_slowF);
                fflush(g_slowF);
                }
            }
            s6WasIn = anyAtk;
            { double tb1 = qms(); if (tb1 - tb0 > fpBlkMax) fpBlkMax = tb1 - tb0; }
            if (s6Pend)
            {
                if ((LONG)g_hitSeq != s6Hit)
                {
                    s6Pend = 0;
                    if (g_slowActive)
                    {
                        g_slowActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                        if (g_slowF) { fprintf(g_slowF, "t=%u 6act-CONFIRM: ?? -> ????\n", (unsigned)now); fflush(g_slowF); }
                    }
                }
                else if (now - s6At >= S6_CONFIRM_MS)
                {
                    s6Pend = 0;
                    if (g_slowF) { fprintf(g_slowF, "t=%u 6act-CONFIRM: ??? -> ??????\n", (unsigned)now); fflush(g_slowF); }
                }
            }
        }

        // 5.5) ? ???????: ?????"????"????????????
        if (g_negHookOk)
        {
            LONG np = g_negPlayerN;
            if (now - lastNegRep > 5000)
            {
                lastNegRep = now;
                if (g_slowF)
                {
                    // 顺便把最近被判定过的对象打出来(3 个), 以及玩家自己的三种形式 ——
                    // 这样下次一眼能确认"哪个地址才是玩家", 强制无敌就能精确只打玩家。
                    fprintf(g_slowF, "t=%u INVSTAT calls=%ld playerNeg=%ld forced=%ld obj[%llX %llX %llX] 玩家chr=%llX ctr=%llX inv=%llX\n",
                            (unsigned)now, g_negAnyN, g_negPlayerN, (long)g_invForcedN,
                            (unsigned long long)g_invObj[(g_invN - 1) & 23],
                            (unsigned long long)g_invObj[(g_invN - 2) & 23],
                            (unsigned long long)g_invObj[(g_invN - 3) & 23],
                            (unsigned long long)player_chr(), (unsigned long long)ctr_of_player(), g_playerInv);
                    fflush(g_slowF);
                }
            }
            if (np != lastNegN)
            {
                lastNegN = np;
                int freshDodge = ((DWORD)(now - lastDodgePress) <= 500) ? 1 : 0;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u NEGATE#%ld invincible-check=YES dodgePressedAgo=%ums -> %s\n",
                            (unsigned)now, np, (unsigned)(now - lastDodgePress),
                            freshDodge ? "TRIGGER" : "ignored (not a dodge press)");
                    fflush(g_slowF);
                }
                if (freshDodge && !g_slowActive)
                {
                    g_slowStart = now;
                    g_slowActive = 1;
                    g_abortActive = 0;          // ???????, ???????
                    InterlockedIncrement(&g_slowCount);
                    set_game_speed(SLOW_SPEED);
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u >>> SLOW START (confirmed dodge) %ums @0.10\n",
                                (unsigned)now, (unsigned)g_slowDur);
                        fflush(g_slowF);
                    }
                }
            }
        }

        // 5.6) ?? ????????? -> ???????????
        //       ??? + ???  =  ?????????  =  ????
        if (g_resHookOk == 1)
        {
            LONG rs = g_resSeq;
            if (rs != lastResSeq)
            {
                lastResSeq = rs;
                unsigned long long a1 = g_resA1, a2 = g_resA2;
                unsigned char d9 = 0xFF;
                int hpNow = -1;
                if (a2 != 0) rd_u8(a2 + 0xD9, &d9);
                {
                    unsigned long long pch = player_chr();
                    if (pch != 0) rd_u32(pch + 0x130, &hpNow);
                }
                lastResLogged = rs;
                resPending = 1;
                resPendingAt = now;
                resPendingHit = g_resHitSnap;      // ??????????????
                resPendingHp  = g_resHpSnap;       // ??????????
                resPendingPlayer = (g_playerA1 != 0 && a1 == g_playerA1) ? 1 : 0;
                if (resPendingPlayer) g_lastPlayerResMs = now;   // ★v110
                {
                    DWORD dodgeAgo = (DWORD)(now - lastDodgePress);
                    int freshDodge = (dodgeAgo <= 800) ? 1 : 0;
                    // ?? ??: ???**??**???????
                    //    ????"?????"?? g_resHpSnap(???)?
                    //    ?????????(? 0~2ms), ?????????
                    //    ??????????? -> ???????????? -> ??????
                    //    (??????: ????, ??????????, ?????)
                    int liveHp = -1;
                    {
                        unsigned long long pch9 = player_chr();
                        if (pch9 != 0) rd_u32(pch9 + 0x130, &liveHp);
                    }
                    int thisResolveDamaged = (liveHp >= 0 && g_resHpSnap > 0 && liveHp < g_resHpSnap) ? 1 : 0;
                    int suppressed = (now < g_hitSuppressUntil) ? 1 : 0;
                    // 3act ??: ??"????????"??????(??????)
                    unsigned a3 = g_resA3v, a4 = g_resA4v;
                    int attackLike = (a3 == 0x403u || a3 == 0x404u || a3 == 0x3E9u) ? 1 : 0;
                    int needAtk = (strstr(g_mode, "3act") != NULL) ? 1 : 0;
                    // 4act ??: ?? + "???????"
                    //   ???????????; ???????????????
                    //   ????: ??? 3/3 ??, ??? 0/6 ???
                    int needFreshAnim = (strstr(g_mode, "4act") != NULL) ? 1 : 0;
                    DWORD animAge = (dbLastChange != 0) ? (now - dbLastChange) : 999999;
                    int animFresh = 1;
                    if (needFreshAnim) animFresh = (dbAnim >= 0 && animAge <= 900) ? 1 : 0;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u RESOLVE#%ld a1=%llX a2=%llX hitD9=%d arg3=0x%X arg4=0x%X php=%d snapHp=%ld dodgeAgo=%ums animAge=%ums freshDodge=%d%s%s%s\n",
                                (unsigned)now, rs, a1, a2, (int)d9, a3, a4,
                                hpNow, (long)g_resHpSnap, (unsigned)dodgeAgo, (unsigned)animAge, freshDodge,
                                thisResolveDamaged ? " THIS_HIT" : "",
                                suppressed ? " SUPPRESSED" : "",
                                attackLike ? " ATK" : "", animFresh ? " FRESH" : " STALE");
                        fflush(g_slowF);
                    }
                    // ?? v65 ??: ????????"??? = ????"??
                    //    ?????: ???????**???**????????????
                    //    ??? -> 6act ???????; ??? -> ??????????, ?????
                    // ? v66: ???"?????????"???(?????????????)
                    if (g_slowF && !thisResolveDamaged && freshDodge && resPendingPlayer)
                    {
                        unsigned int tm2 = 0, ta2 = 0, tv2 = 0;
                        int tcs = -1, trem = 0, tnpc = -1, tws = 0, twe = 0, tok = 0;
                        float td2 = player_enemy_dist();
                        unsigned long long tc2 = enemy_ctr_best();
                        if (tc2 != 0)
                            tok = enemy_attack_phase(tc2, &tm2, &ta2, &tcs, &trem, &tv2, &tnpc,
                                                     S6_LEAD_CS, &tws, &twe);
                        fprintf(g_slowF, "t=%u RDODGE ???????: ??anim=%d t=%d.%02ds ???=%d (win %d.%02d-%d.%02d model=%u var=%u npc=%d) dist=%.2f\n",
                                (unsigned)now, ta2, tcs / 100, (tcs < 0 ? 0 : tcs % 100), tok,
                                tws / 100, (tws < 0 ? 0 : tws % 100), twe / 100, (twe < 0 ? 0 : twe % 100),
                                tm2, tv2, tnpc, td2);
                        fflush(g_slowF);
                    }
                    // ? ?"??????"?????(a2 ???)?????
                    //   "??????" ? "????????" ?????????????
                    if (g_slowF && a2 != 0)
                    {
                        float rdist = -1.0f;
                        {
                            unsigned long long pctr8 = ctr_of_player(), ectr8 = enemy_ctr_best();
                            float pa8[3], ea8[3];
                            if (pctr8 != 0 && ectr8 != 0 && ctr_pos(pctr8, pa8) && ctr_pos(ectr8, ea8))
                            {
                                float dx = ea8[0]-pa8[0], dy = ea8[1]-pa8[1], dz = ea8[2]-pa8[2];
                                rdist = sqrtf(dx*dx + dy*dy + dz*dz);
                            }
                        }
                        fprintf(g_slowF, "   mode=%s dist=%.2f THIS_HIT=%d SUPPRESSED=%d\n",
                                g_mode, rdist, thisResolveDamaged, suppressed);
                        if (g_resRet != 0)
                            fprintf(g_slowF, "   ret=%llX  RVA=0x%llX\n",
                                    g_resRet, (unsigned long long)(g_resRet - g_base));
                        fp_hex(g_slowF, "   H", a2, 0x300);
                        fflush(g_slowF);
                    }
                    // ? ?????? ?? ????"???????", ?????
                    //   ?????????????(???/???), ????????
                    if (!useArTrigger && strstr(g_mode, "6act") == NULL && freshDodge && !thisResolveDamaged && !suppressed && !g_slowActive && (!needAtk || attackLike) && (!needFreshAnim || animFresh))
                    {
                        g_slowStart = now;
                        g_slowActive = 1;
                        g_abortActive = 0;
                        InterlockedIncrement(&g_slowCount);
                        set_game_speed(SLOW_SPEED);
                        if (g_slowF)
                        {
                            fprintf(g_slowF, "t=%u >>> SLOW START (instant, confirming) %ums @0.10\n",
                                    (unsigned)now, (unsigned)g_slowDur);
                            fflush(g_slowF);
                        }
                    }
                    // ? ??"?????????"?????? ?? ??????**??**???
                    //   ????????????????: ?????????,
                    //   ???????????????????
                    if (!useArTrigger && strstr(g_mode, "6act") == NULL && freshDodge && !thisResolveDamaged && !suppressed && fastPlayer && (!needAtk || attackLike) && (!needFreshAnim || animFresh))
                    {
                        float len5 = 0.0f;
                        int   aid5 = -1;
                        unsigned long long pc5 = ctr_of_player();
                        if (pc5 != 0) aid5 = anim_of_ctr(pc5, &len5);
                        if (aid5 >= 0)
                        {
                            faOn = 1;
                            faAnim = aid5;
                            faElen = len5;
                            faUntil = now + FAST_MS;
                            if (g_slowF)
                            {
                                fprintf(g_slowF, "t=%u ACTWINDOW open anim=%d elen=%.2f (????????)\n",
                                        (unsigned)now, aid5, len5);
                                fflush(g_slowF);
                            }
                        }
                    }
                }
            }
            else if (resPending)
            {
                int hpNow = -1;
                {
                    unsigned long long pch = player_chr();
                    if (pch != 0) rd_u32(pch + 0x130, &hpNow);
                }
                // ??"??????" ?? g_hitSeq ?"????"?????, ????????,
                // ?????????????????????
                int dmged = (hpNow >= 0 && resPendingHp > 0 && hpNow < resPendingHp) ? 1 : 0;
                if (dmged)
                {
                    // ???????????(??????) -> ????????????
                    resPending = 0;
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u CONFIRM: DAMAGE hp %ld->%d seq=%ld snap=%ld -> %s\n",
                                (unsigned)now, (long)resPendingHp, hpNow, (long)g_hitSeq, (long)resPendingHit,
                                g_slowActive ? "ABORT ??????" : "no slow was running");
                        fflush(g_slowF);
                    }
                    if (g_slowActive)
                    {
                        g_slowActive = 0;
                        g_abortActive = 0;
                        set_game_speed(1.0);
                        InterlockedIncrement(&g_abortCount);
                    }
                }
                else if (now - resPendingAt >= 40)
                {
                    resPending = 0;
                    // ★v76: 这一击结算到我头上、40ms 内没有掉血 = 被无敌帧/格挡吃掉 = 真的躲开了
                    if (resPendingPlayer)
                    {
                        g_negAt = (LONG)now;
                        // ★★ v108: 用户要求 —— "受击"不能只看掉血: 义父的踩**没有伤害但有受击动作**。
                        //   判据: 这一击结算到玩家头上、40ms 没掉血, 且玩家**当前并不在无敌帧窗口里**
                        //   (= 不是靠垫步无敌帧吃掉的) -> 那就是被打到了(受击动作) -> 缓速当场作废。
                        {
                            unsigned long long pcv = ctr_of_player(), obv = 0;
                            int   aidv = -1, inIfr = 0;
                            float tv = 0.0f;
                            if (pcv != 0 && rd_ptr(pcv + 0x10, &obv) && obv != 0)
                            {
                                rd_u32(obv + 0x20, &aidv);
                                if (aidv >= 0 && safe_read(obv + 0x24, &tv, 4))
                                {
                                    unsigned short sv2 = 0, ev2 = 0;
                                    int csv = (int)(tv * 100.0f + 0.5f);
                                    if (ifr_find(aidv, &sv2, &ev2) && csv >= (int)sv2 && csv <= (int)ev2)
                                        inIfr = 1;
                                }
                            }
                            // ★v113: 如果这一下是被**我们自己给的全程无敌**挡掉的, 那不算"受击动作",
                            //   不能取消缓速(否则缓速一挨打就自己没了, 正是用户烦的那个手感)
                            if (!inIfr && g_slowActive && (LONG)(g_invForceUntil - (LONG)now) <= 0)
                            {
                                g_slowActive = 0; g_abortActive = 0;
                                set_game_speed(1.0);
                                InterlockedIncrement(&g_abortCount);
                                if (g_slowF)
                                {
                                    fprintf(g_slowF, "t=%u HITREACT-CANCEL: 这一下打到了我(没掉血但也不在无敌帧) -> 缓速作废 anim=%d\n",
                                            (unsigned)now, aidv);
                                    fflush(g_slowF);
                                }
                            }
                        }
                        // ★v90: 如果这一下不是"刚按过闪避"吃掉的, 那就是**格挡**。
                        //   格挡之后玩家再按闪避, 不该被当成"躲开这一下"而缓速。
                        if (lastDodgePress == 0 || (DWORD)(now - lastDodgePress) > 400)
                        {
                            s6BlockAt = now;
                            if (g_slowF)
                            {
                                fprintf(g_slowF, "t=%u 这一下是格挡掉的(按下闪避在 %ums 前) -> 之后 400ms 内的闪避不算数\n",
                                        (unsigned)now, (unsigned)(now - lastDodgePress));
                                fflush(g_slowF);
                            }
                        }
                    }
                    if (g_slowF)
                    {
                        fprintf(g_slowF, "t=%u CONFIRM: NO DAMAGE hp %ld->%d seq=%ld -> ???????\n",
                                (unsigned)now, (long)resPendingHp, hpNow, (long)g_hitSeq);
                        fflush(g_slowF);
                    }
                }
            }
            if (now - lastResRep > 10000)
            {
                lastResRep = now;
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u RESSTAT resolves=%ld playerMan=%llX\n",
                            (unsigned)now, rs, g_playerMan);
                    fflush(g_slowF);
                }
            }
        }

        if (newDodge && !g_slowActive && !g_negHookOk && g_resHookOk != 1)
        {
            float ppos[3] = {0, 0, 0}, epos[3] = {0, 0, 0};
            float dist = -1.0f;
            int   eAnim = -1;
            DWORD stable = 0;
            unsigned long long pctr = ctr_of_player();
            unsigned long long ectr = enemy_ctr_best();
            if (pctr != 0 && ectr != 0 && ctr_pos(pctr, ppos) && ctr_pos(ectr, epos))
            {
                float dx = epos[0]-ppos[0], dy = epos[1]-ppos[1], dz = epos[2]-ppos[2];
                dist = sqrtf(dx*dx + dy*dy + dz*dz);
            }
            if (ectr != 0) eAnim = anim_of_ctr(ectr, NULL);
            stable = anim_stable_ms(now, eAnim);

            fp_dump("DODGE", now, dist, ectr, (g_targetMan != 0) ? g_targetMan : 0);

            // ??:
            //   ????????? -> ??"?????????"(?????????)
            //   ????(????) -> ????"??????"?????
            // ??:
            //   ?????????????(>=3 ?) -> ????(????????)
            //   ?????????
            //   ???????(???) -> ????"??????"?????
            int   learned = 0, typeKey = 0;
            LONG  setN = 0;
            {
                unsigned long long dch = 0;
                if (ectr != 0 && rd_ptr(ectr + 0x18, &dch)) typeKey = cmap_get(dch);
                {
                    AtkSet* ts = (typeKey != 0) ? at_get(typeKey, 0) : NULL;
                    AtkSet* gs = at_get(0, 0);
                    if (ts != NULL && ts->n >= 3) { learned = at_has(ts, eAnim); setN = ts->n; }
                    else if (gs != NULL)          { learned = at_has(gs, eAnim); setN = gs->n; }
                }
            }
            int gate = 0;
            if (dist >= 0.0f && dist < DIST_MAX)
            {
                if (setN > 0) gate = learned ? 1 : 0;
                else          gate = (eAnim >= 0 && stable <= ANIM_FRESH_MS) ? 1 : 0;
            }
            if (g_slowF)
            {
                fprintf(g_slowF, "t=%u DODGE dist=%.2f anim=%d stable=%ums type=%d learned=%d setN=%ld gate=%d\n",
                        (unsigned)now, dist, eAnim, (unsigned)stable, typeKey, learned, setN, gate);
                fflush(g_slowF);
            }
            if (gate)
            {
                g_slowStart = now;
                g_slowActive = 1;
                g_abortActive = 1;
                g_abortUntil = now + ABORT_MS;
                g_abortHit = (LONG)g_playerHitMs;
                InterlockedIncrement(&g_slowCount);
                set_game_speed(SLOW_SPEED);
                if (g_slowF)
                {
                    fprintf(g_slowF, "t=%u >>> SLOW START dist=%.2f anim=%d %ums @0.10 (abort window %dms)\n",
                            (unsigned)now, dist, eAnim, (unsigned)g_slowDur, ABORT_MS);
                    fflush(g_slowF);
                }
            }
        }
    }
    return 0;
}

static DWORD WINAPI flusher(LPVOID)
{
    fopen_s(&g_log, LOGPATH, "w");
    if (g_log == NULL) return 0;

    fprintf(g_log, "# hook start (probe @ sekiro.exe+B6E6A0) base=%llX player=%llX\n",
            g_base, g_player);
    fflush(g_log);
    fprintf(g_log, "# ts_hook=%ld  (1=frame-timescale hook installed, 0=failed, -1=not yet)\n",
            (long)g_ts_ok);
    fflush(g_log);

    LONG last = 0;
    DWORD refresh = 0;
    DWORD endAt = GetTickCount() + 1800000;
    LONG statusPrinted = 0;
    char mode[32] = "none";

    while (GetTickCount() < endAt)
    {
        DWORD now = GetTickCount();
        if (statusPrinted == 0 && g_install_ok != -1)
        {
            fprintf(g_log, "# install=%s (??%s)\n", g_install_ok ? "OK" : "FAILED",
                    g_install_ok ? "??" : "???");
            if (!g_install_ok)
            {
                fputs("# ????????: ", g_log);
                for (int i = 0; i < PROLOG_LEN; i++) fprintf(g_log, "%02X ", g_probe[i]);
                fputs("\n", g_log);
            }
            fflush(g_log);
            statusPrinted = 1;
        }
        if (now - refresh > 1000)
        {
            refresh = now;
            g_player = resolve_player();
            FILE* mf = NULL;
            if (fopen_s(&mf, MODEPATH, "r") == 0 && mf != NULL)
            {
                if (fgets(mode, sizeof(mode), mf) == NULL) strcpy_s(mode, "none");
                for (char* q = mode; *q; q++) { if (*q == '\r' || *q == '\n') { *q = 0; break; } }
                fclose(mf);
            }
        }

        LONG head = g_head;
        while (last < head && last < 5000)
        {
            Rec r = g_buf[last % NBUF];
            unsigned long long chr = r.chr;
            if (chr != 0) rd_u32(chr + 0x130, &r.hp1);
            last++;
            // ? 500 ?????????? chr ?????????????
            if (last > 300 && !r.isPlayer) continue;
            fprintf(g_log,
                    "t=%u mode=%s chr=%llX isPlayer=%d flags=%02X gnodmg=%d hp=%d->%d dmg=%d f28=%d f1ca=%X f2c=%d "
                    "pdmg=%d f208=%d f20c=%d "
                    "v=%.3f/%.3f/%.3f/%.3f/%.3f a1=%llX a2=%llX a3=%llX",
                    r.t, mode, r.chr, r.isPlayer, r.flags, r.gnodmg, r.hp0, r.hp1, r.dmg, r.f28, r.f1ca, r.f2c,
                    r.pdmg, r.f208, r.f20c,
                    r.v[0], r.v[1], r.v[2], r.v[3], r.v[4], r.arg1, r.arg2, r.arg3);
            if (r.hasDump)
            {
                fputs(" D=", g_log);
                for (int i = 0; i < DUMP_BYTES; i++) fprintf(g_log, "%02X", r.dump[i]);
            }
            if (r.hasAtk)
            {
                fputs(" A=", g_log);
                for (int i = 0; i < ATK_BYTES; i++) fprintf(g_log, "%02X", r.atk[i]);
            }
            fputs("\n", g_log);
        }
        if (last >= 5000) break;
        fflush(g_log);
        Sleep(150);
    }
    fprintf(g_log, "# hook stop  total_calls=%ld\n", (long)g_total);
    fclose(g_log);
    return 0;
}

// ---------- ??? ----------
static void suspend_others(bool suspend)
{
    static HANDLE th[1024];
    static int    nth = 0;
    DWORD me = GetCurrentThreadId();
    if (suspend)
    {
        nth = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te; te.dwSize = sizeof(te);
        if (Thread32First(snap, &te))
        {
            do {
                if (te.th32OwnerProcessID == GetCurrentProcessId() &&
                    te.th32ThreadID != me && nth < 1024)
                {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                    if (h) { SuspendThread(h); th[nth++] = h; }
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    else
    {
        for (int i = 0; i < nth; i++) { ResumeThread(th[i]); CloseHandle(th[i]); }
        nth = 0;
    }
}

static bool install(void)
{
    unsigned long long tgt = g_base + F_TARGET;

    static const unsigned char expect[PROLOG_LEN] = {
        0x48,0x8B,0xC4, 0x55, 0x53, 0x56, 0x57, 0x41,0x56, 0x41,0x57,
        0x48,0x8D,0x68,0xB8
    };
    unsigned char orig[PROLOG_LEN];
    if (!safe_read(tgt, orig, PROLOG_LEN)) { g_probe_ok = 0; return false; }
    memcpy((void*)g_probe, orig, PROLOG_LEN);
    if (memcmp(orig, expect, PROLOG_LEN) != 0) { g_probe_ok = 0; return false; }  // ????????
    g_probe_ok = 1;

    unsigned char* tr = (unsigned char*)VirtualAlloc(NULL, 64,
                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return false;
    memcpy(tr, orig, PROLOG_LEN);
    // jmp qword ptr [rip+0] ; imm64
    tr[PROLOG_LEN + 0] = 0xFF;
    tr[PROLOG_LEN + 1] = 0x25;
    *(unsigned int*)(tr + PROLOG_LEN + 2) = 0;
    *(unsigned long long*)(tr + PROLOG_LEN + 6) = tgt + PROLOG_LEN;
    g_tramp = tr;

    unsigned char patch[14];
    patch[0] = 0xFF; patch[1] = 0x25;
    *(unsigned int*)(patch + 2) = 0;
    *(unsigned long long*)(patch + 6) = (unsigned long long)(uintptr_t)&hook_stub;

    DWORD oldp = 0;
    if (!VirtualProtect((LPVOID)(uintptr_t)tgt, PROLOG_LEN, PAGE_EXECUTE_READWRITE, &oldp))
        return false;
    suspend_others(true);
    memcpy((void*)(uintptr_t)tgt, patch, 14);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)tgt, PROLOG_LEN);
    suspend_others(false);
    VirtualProtect((LPVOID)(uintptr_t)tgt, PROLOG_LEN, oldp, &oldp);
    return true;
}

extern "C" float ts_scale(void)
{
    InterlockedIncrement(&g_tsCalls);
    return g_timeScale;
}

// ?? 0xA3829D ? `movss %xmm6,0x10d0(%rsi)`???????"?????"???????
static bool install_ts(void)
{
    unsigned long long tgt = g_base + 0xA3829DULL;
    static const unsigned char expect[12] = {
        0xF3,0x0F,0x11,0xB6,0xD0,0x10,0x00,0x00,   // movss %xmm6,0x10d0(%rsi)
        0x41,0x83,0xCD,0xFF                        // or $0xffffffff,%r13d
    };
    unsigned char orig[12];
    if (!safe_read(tgt, orig, 12)) return false;
    if (memcmp(orig, expect, 12) != 0) return false;

    unsigned char* tr = (unsigned char*)VirtualAlloc(NULL, 64,
                            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return false;
    memcpy(tr, orig, 12);
    tr[12] = 0xFF; tr[13] = 0x25;
    *(unsigned int*)(tr + 14) = 0;
    *(unsigned long long*)(tr + 18) = tgt + 12;
    g_tstramp = tr;

    unsigned char patch[12];
    patch[0] = 0x48; patch[1] = 0xB8;
    *(unsigned long long*)(patch + 2) = (unsigned long long)(uintptr_t)&ts_stub;
    patch[10] = 0xFF; patch[11] = 0xE0;

    DWORD oldp = 0;
    if (!VirtualProtect((LPVOID)(uintptr_t)tgt, 12, PAGE_EXECUTE_READWRITE, &oldp)) return false;
    suspend_others(true);
    memcpy((void*)(uintptr_t)tgt, patch, 12);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)(uintptr_t)tgt, 12);
    suspend_others(false);
    VirtualProtect((LPVOID)(uintptr_t)tgt, 12, oldp, &oldp);
    return true;
}

// ★v85: 本进程有没有"已经建好的正常游戏窗口"(客户区 >= 640x360)
//   用来避免在窗口/交换链还没建好时挂起游戏线程(那会把窗口搞成缩在角落)
static BOOL CALLBACK win_probe(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    RECT r;
    int* found = (int*)lp;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (IsIconic(h)) return TRUE;
    if (!GetClientRect(h, &r)) return TRUE;
    if ((r.right - r.left) >= 640 && (r.bottom - r.top) >= 360)
    {
        *found = 1;
        return FALSE;
    }
    return TRUE;
}

static int game_window_ok(void)
{
    int found = 0;
    EnumWindows(win_probe, (LPARAM)&found);
    return found;
}

static DWORD WINAPI setup(LPVOID)
{
    Sleep(3000);
    for (int i = 0; i < 20 && g_player == 0; i++)
    {
        g_player = resolve_player();
        if (g_player == 0) Sleep(500);
    }

    // ★★ v85: 等"游戏窗口真的建好"再打代码补丁。
    //   打补丁时会 suspend_others(true) 短暂挂起游戏其他线程; 如果这时候游戏的窗口/
    //   D3D 交换链还在初始化, 就会被搞成"窗口缩在角落/锁死"那种老毛病(用户报过两次)。
    //   判据: 本进程存在一个客户区 >= 640x360 的可见窗口。最多等 30 秒, 超时也继续
    //   (宁可偶尔再犯, 也不能让 mod 完全不装)。
    for (int i = 0; i < 60; i++)
    {
        if (game_window_ok()) break;
        Sleep(500);
    }

    // ★v96: 挂 D3D11 Present(凸面镜特效)。放在"窗口已经建好"之后做,
    //   尽量不掺和游戏自己的 D3D 初始化; vtable 是共享的, 晚一点挂也一样有效。
    if (fx_on)
    {
        if (fx_install()) FX_LOG("t=%u FX: Present 钩子就绪\n", (unsigned)GetTickCount());
        else              FX_LOG("t=%u FX: Present 钩子没挂上(特效不会出现, 其余功能不受影响)\n", (unsigned)GetTickCount());
    }
    else FX_LOG("t=%u FX: MODE 里写了 nofx, 特效关闭\n", (unsigned)GetTickCount());

    // ★v104: 音频缓速默认**不挂**了 —— 用户已经连崩两次, 而且第二次 FMOD 调用
    //   其实是关着的(说明是那个 System::update 代码补丁 stub 本身的风险)。
    //   想再试音频, 就在 MODE 里写 fmodtry(带着 nofmod 就不会挂)。
    read_mode(g_mode, sizeof(g_mode));      // 先真的读一遍 MODE
    // ★v115: 音频缓速默认**开**(用户说这条很重要); 写 nofmod 可关。
    //   这次改成: 只用栈保存参数的可重入 stub + 每个窗口只调 1~2 次 OverrideFrequency。
    // ★v124: 音频改成"扫描 + 只读验证"的保守路线, 而且**默认只探测不启用**:
    //   写 fmodtry 才会真的去改音频; 默认把它当"观察", 不会碰声音。
    // ★★★ v130: 发布版 —— FMOD 那条线**彻底停用**(连入口都不再调用)。
    //   它在你的机器上崩过 4 次(FMOD 对象只能由游戏自己的线程访问, 打代码补丁那条也崩过 3 次),
    //   公开版本绝不带着这种代码跑。源码里剩下的 FMOD 函数暂时保留(只是没人调用),
    //   等下一步做代码清理时整段删除。
    g_fmodOn = 0;
    FX_LOG("t=%u FMOD: 已停用(发布版不带音频注入)\n", (unsigned)GetTickCount());

    // ★★★ v130: 版本/签名自检 —— 发布版必须"版本不对就干净退出", 不能乱写内存。
    {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_base;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((unsigned char*)g_base + dos->e_lfanew);
        unsigned char p1[16], p2[16], p3[16];
        int s1, s2, s3;
        static const unsigned char e1[4] = {0x48,0x8B,0xC4,0x55};        // 0xB6E6A0 伤害函数
        static const unsigned char e2[4] = {0x48,0x89,0x5C,0x24};        // 0xB68FF0 攻击结算
        static const unsigned char e3[4] = {0x48,0x83,0xEC,0x28};        // (备用签名)
        s1 = (safe_read(g_base + 0xB6E6A0ULL, p1, 4) && memcmp(p1, e1, 4) == 0) ? 1 : 0;
        s2 = (safe_read(g_base + 0xB68FF0ULL, p2, 4) && memcmp(p2, e2, 4) == 0) ? 1 : 0;
        s3 = (safe_read(g_base + 0xB6E892ULL, p3, 4) && memcmp(p3, e3, 4) == 0) ? 1 : 0;
        FX_LOG("=== SELFCHECK PE时间戳=%08lX 签名[0xB6E6A0=%d 0xB68FF0=%d 0xB6E892=%d] ===\n",
               (unsigned long)nt->FileHeader.TimeDateStamp, s1, s2, s3);
        if (!s1 || !s2)
            FX_LOG("!!! 游戏版本/签名不匹配 —— 代码补丁会被各自跳过(只保留只读功能), 别把日志当正常\n");
    }

    HANDLE h = CreateThread(NULL, 0, flusher, NULL, 0, NULL);
    if (h) CloseHandle(h);
    HANDLE h2 = CreateThread(NULL, 0, chrwatch, NULL, 0, NULL);
    if (h2) CloseHandle(h2);
    HANDLE h3 = CreateThread(NULL, 0, fastkey, NULL, 0, NULL);
    if (h3) CloseHandle(h3);

    // ?????????? / ?????????? 5 ?????? 12 ?
    for (int i = 0; i < 12; i++)
    {
        // ★v85: MODE 里写 safe = 完全不装"代码补丁"类钩子(只留 IAT 的时钟/按键钩子)。
        //   用来二分定位: 如果 safe 下窗口正常、6act 下窗口又缩在角落, 那就确认是
        //   我们打代码补丁(会短暂挂起游戏线程)造成的。
        char msafe[32];
        read_mode(msafe, sizeof(msafe));
        int safeAll = (strstr(msafe, "safe") != NULL) ? 1 : 0;

        if (safeAll)
        {
            install_clock_hooks();
            resolve_timescale();
            install_input_hooks();
            g_tm_ok = -1;
            g_resHookOk = -1;
            g_arHookOk = -1;
            g_install_ok = g_slowCount >= 0 ? 1 : 1;   // 标记"已处理"
            return 0;
        }

        if (install())
        {
    g_install_ok = 1;
    
    install_clock_hooks();
    resolve_timescale();   // ???????"??????"(????, ???????)
            install_input_hooks();
            g_tm_ok = install_targetman() ? 1 : 0;
            // 0xBE00D0/0xBE0170 ???"????"??????(?? 12 ???? 0),
            // ????????"???????????"???, ?????
            // ★v91: 重新装上。以前"证伪"的是"读它能不能认出闪避"; 现在我们要的是
            //   **在保护窗口内强制它返回 1**, 让游戏自己把这一击判成"打不动"。
            install_inv_hooks();
            {
                // MODE.txt ?? safe ??????(????, ??????)
                char m[32];
                read_mode(m, sizeof(m));
                if (strstr(m, "safe") == NULL)
                {
                    install_resolve_hook();
                    install_attackres_hook();
                }
                else
                {
                    g_resHookOk = -1;   // ????"??????"
                }
            }
            // ts hook is installed lazily, only when MODE=4slow
            return 0;
        }
        Sleep(5000);
    }
    g_install_ok = 0;
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)lpReserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        g_base = (unsigned long long)(uintptr_t)GetModuleHandleA(NULL);
        HANDLE h = CreateThread(NULL, 0, setup, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}

