// 空表版本(发布用)。
// 原表内容来自第三方仓库 thisguymartin/sekiro-deflect-observer 的 incoming-coverage.json,
// 按"只发布我们自己找到的数据"的原则, 公开版不包含他人数据。
// 这里只保留类型定义, 表内容为空 —— "社区表窗口"这条判据自然失效;
// 我们自己的判据(档案判定帧 / 放弹丸 / 学到的命中状态 / 回溯判定)完全不受影响。
#pragma once
typedef struct { unsigned int npcId; unsigned int variation; } NpcVarEntry;
typedef struct { unsigned short model; unsigned int variation; unsigned int anim;
                 unsigned short startCs; unsigned short endCs; unsigned char resp; unsigned char pad; } AtkRec;
static const NpcVarEntry g_npcVar[] = { {0u, 0u} };
#define NPCVAR_N 1
static const AtkRec g_atkRec[] = { {0u, 0u, 0u, 0u, 0u, 0u, 0u} };
#define ATKREC_N 1