# 契约：QEI 左右命名对调（已兑现）

状态：**ACCEPTED**（2026-07-17 当日冻结、当日兑现）
冻结提交：`6943172`（原为 `docs/给硬件组的修改方案.md` §5，硬件文档瘦身后迁至此处存档）
兑现提交：`fd1ca92`
背景：硬件组网表证实 U33/U34 插座上电机与编码器左右命名交叉（M1，`docs/网表对照结论.md` §3）；
用户裁定「硬件改动最小化，固件能改名吸收的固件吸收」，定义 U33=左轮、U34=右轮（驾驶员视角）。

## 契约正文（冻结时原文）

改动定义（唯一改动点 = `board.syscfg` 两行 `$name` + 注释同步）：

| # | 断言（施工后必须逐条兑现） | 冻结前实测基线 | 兑现结果 |
|---|---|---|---|
| C1 | `board.syscfg:204` `QEI1.$name` 由 `"QEI_LEFT"` 改 `"QEI_RIGHT"`；`board.syscfg:221` `QEI2.$name` 由 `"QEI_RIGHT"` 改 `"QEI_LEFT"`；两处注释块同步改写（含 U33/U34 网表依据） | 当时 204=`"QEI_LEFT"`、221=`"QEI_RIGHT"`（已读行确认） | ✅ 兑现（改后行号漂移至 207/226，系注释块增行，auditor 判不构成偏差） |
| C2 | 重建后 `Debug/ti_msp_dl_config.h`：`QEI_LEFT_INST` = **TIMG8**、`QEI_RIGHT_INST` = **TIMG9** | 当时 :116=TIMG9、:131=TIMG8（已读行确认） | ✅ 兑现（:116=QEI_RIGHT_INST TIMG9、:131=QEI_LEFT_INST TIMG8，auditor 独立复读） |
| C3 | `hc-team` 源码除 `mspm0_runtime.c:8` 注释同步外**零改动**（board.c:17-18 与 mspm0_runtime.c:254/257 均走 `QEI_*_INST` 宏，已核全部使用点） | grep 全仓 `QEI_LEFT\|QEI_RIGHT`：源码命中仅上述 3 处宏使用 + 1 处注释 | ✅ 兑现（diff 仅注释 3 行） |
| C4 | 主机测试 **109 PASS / 0 FAIL** 不变；固件构建 exit 0 / 0 诊断 | 当时 109/0、exit 0（封包报告 §1） | ✅ 兑现（auditor 独立复跑同值） |
| C5 | 文档同步：计划表 §4.1（左轮编码器 = U33/PB10/PB11/TIMG8）、`网表对照结论.md` §3 标注方案 a 已执行 | — | ✅ 兑现 |

不改的东西（明确划界）：`s_direction_sign[]`（等 H3 手推实测）、电机侧映射（`motor_hw.c`
一字不动——U33 的电机本来就叫左）、历史计划文档（`plan_qei_gray_pinmux.md` 的 E02 是当时
事实，不改写历史）。

生效条件：本契约随文档提交冻结后施工；HW-4 若被硬件组否定（模块非经典脚序），本契约作废重议。
**HW-4 确认状态：截至存档时硬件组尚未回复——若否定，按「作废重议」条款回滚 `$name` 对调。**
