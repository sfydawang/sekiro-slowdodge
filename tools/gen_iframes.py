# 从玩家(c0000)的动画档案里生成"无敌帧表": sekiro_iframes.h
#   只取 IFrames 类事件(950 IFrames_MistRaven / 951 IFrames_DuringAction /
#   952 IFrames_ThrowAtkStillHurts / 953 / 954 IFrames)
import json, os, collections

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'dist', 'game-analysis')
OUT = os.path.join(HERE, 'sekiro_iframes.h')
IFR = (950, 951, 952, 953, 954)

rows = []
doc = json.load(open(os.path.join(SRC, 'c0000.anibnd.dcx.timelines.json'), encoding='utf-8'))
all_anims = [a for tl in doc.get('timelines', []) for a in tl.get('animations', [])]
by_id = {a['id']: a for a in all_anims}

def resolve(a, depth=0):
    """★v89: 动画可以"引用"另一个动画(imports_animation)。
       例如垫步 213300 自己没有事件, 它 import 了 213301 —— 无敌帧在 213301 里。
       以前不解析引用, 于是 213300 被当成"没有无敌帧"漏掉了(你实测用了 12 次)。"""
    if depth > 5:
        return a
    imp = a.get('imports_animation')
    if imp is not None and imp in by_id and (imp != a['id']):
        return resolve(by_id[imp], depth + 1)
    return a

for a in all_anims:
    for tl in (0,):
        pass
    src_anim = resolve(a)
    if src_anim is not a:
        a = dict(src_anim, id=a['id'])     # 用引用目标的事件, 但保留自己的 id
    for tl in (0,):
        pass
    if True:
        for e in a.get('events', []):
            if e.get('type') not in IFR:
                continue
            s, en = e.get('start_seconds'), e.get('end_seconds')
            if en is None:
                continue
            # start 为 -1 表示"从动画第 0 帧起就生效"
            s2 = 0.0 if (s is None or s < 0) else s
            if not (0.0 <= s2 <= en <= 60.0):
                continue
            rows.append((a['id'], int(round(s2 * 100)), int(round(en * 100)), e['type']))

# 同一动画取"覆盖范围最大"的那条(最长无敌)
best = {}
for anim, s, en, t in rows:
    if anim not in best or (en - s) > (best[anim][1] - best[anim][0]):
        best[anim] = (s, en, t)
rows = [(a, v[0], v[1], v[2]) for a, v in sorted(best.items())]

with open(OUT, 'w', encoding='utf-8') as fh:
    fh.write('// 自动生成, 不要手改。生成脚本: gen_iframes.py\n')
    fh.write('// 玩家(c0000)每个动画的无敌帧窗口(单位: 百分之一秒)\n')
    fh.write('#pragma once\n')
    fh.write('typedef struct { int anim; unsigned short s; unsigned short e; unsigned short type; unsigned short pad; } IfrRec;\n')
    fh.write('static const IfrRec g_ifr[] = {\n')
    for anim, s, en, t in rows:
        fh.write('  {%d,%d,%d,%d,0},\n' % (anim, s, en, t))
    fh.write('};\n')
    fh.write('#define IFR_N %d\n' % len(rows))

print('写入 %s : %d 个带无敌帧的动画 (原始事件 %d 条)' % (OUT, len(rows), len(rows_raw) if False else 0))
for a, s, en, t in rows:
    if a in (213300, 213301, 213302, 213303, 213304):
        print('   垫步 anim %-8d 无敌帧 %.2f~%.2fs (type %d)' % (a, s / 100.0, en / 100.0, t))
