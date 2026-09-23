# 从我们自己解包出来的 timelines 生成 C 头文件: sekiro_events.h
#   记录三类事件(全部来自游戏档案本身, 不是社区推测):
#     type 1  = 攻击判定(hitbox)      -> 真正的攻击时间窗
#     type 2  = BulletBehavior        -> 放箭/投弹丸的时刻
#     type 304= ThrowAttackBehavior   -> 投技
import json
import glob
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'dist', 'game-analysis')
OUT = os.path.join(HERE, 'sekiro_events.h')
WANT = (1, 2, 304)

rows = []
for f in sorted(glob.glob(os.path.join(SRC, 'c*.anibnd.dcx.timelines.json'))):
    name = os.path.basename(f).split('.')[0]          # 例如 c7100
    try:
        model = int(name[1:])
    except ValueError:
        continue
    doc = json.load(open(f, encoding='utf-8'))
    for tl in doc.get('timelines', []):
        for a in tl.get('animations', []):
            anim = a.get('id')
            if anim is None or anim < 0:
                continue
            for e in a.get('events', []):
                t = e.get('type')
                if t not in WANT:
                    continue
                s = e.get('start_seconds')
                en = e.get('end_seconds')
                if s is None or en is None or not (0.0 <= s <= en <= 60.0):
                    continue
                j = e.get('behavior_judge_id')
                if j is None:
                    j = e.get('attack_type')
                if j is None:
                    j = -1
                rows.append((model, anim, int(round(s * 100)), int(round(en * 100)), t, int(j)))

rows.sort(key=lambda r: (r[0], r[1], r[4], r[2]))

with open(OUT, 'w', encoding='utf-8') as fh:
    fh.write('// 自动生成, 不要手改。生成脚本: gen_events.py\n')
    fh.write('// 数据来源: 用 sdo_scripts/inspect-game-archives.py 从本机只狼档案里只读解析出来的 TAE 事件\n')
    fh.write('//   1 = 攻击判定(hitbox) 时间窗  2 = 放弹丸(BulletBehavior)  304 = 投技\n')
    fh.write('#pragma once\n')
    fh.write('typedef struct { unsigned short model; unsigned int anim; unsigned short startCs;'
             ' unsigned short endCs; unsigned short type; short judge; } EvRec;\n')
    fh.write('static const EvRec g_ev[] = {\n')
    for model, anim, s, e, t, j in rows:
        fh.write('  {%d,%du,%d,%d,%d,%d},\n' % (model, anim, s, e, t, j))
    fh.write('};\n')
    fh.write('#define EV_N %d\n' % len(rows))

by_type = {}
for r in rows:
    by_type[r[4]] = by_type.get(r[4], 0) + 1
print('写入 %s' % OUT)
print('总行数 %d  明细: %s' % (len(rows), by_type))
print('文件大小 %.1f KB' % (os.path.getsize(OUT) / 1024.0))
