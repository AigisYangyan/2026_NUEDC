# 2DPlatform_LaserStrike 调试测试说明

本文档用于指导 DEBUG 菜单中 Vision_data 的测试执行，并提供验收报告模板。

## 1. 测试对象
测试项：视觉传感器数据流测试（纯视觉数据模块）

前台入口：DEBUG -> Vision_data

后台运行项：RUN_ENTRY_DEBUG_VISION_DATA（backend: DEBUG_Vision_data）

对应任务组：g_tDebugVisionDataTaskGroup

任务调度节拍：
- UI 任务：5ms
- VisionBus 服务：5ms
- Vision_data 遥测：10ms

## 2. 输出通道定义（VOFA）
Vision_data 进入后会清空当前 profile，并注册 4 个 float 通道：

1. pixel_err_X
2. pixel_err_Y
3. frame_dt_ms
4. status

状态值定义：
- 0 = VISION_COORD_STATUS_NONE
- 1 = VISION_COORD_STATUS_TARGET
- 2 = VISION_COORD_STATUS_LOST_TARGET

误差与帧间隔计算规则：
- pixel_err_X = coord.x - 320
- pixel_err_Y = coord.y - 240
- 当 status = LOST_TARGET 时，pixel_err_X/pixel_err_Y 立即归零
- frame_dt_ms 在检测到新帧序号时更新：dt = tick_now - tick_last
- 首帧或无有效元数据时，frame_dt_ms 输出 0

## 3. 测试前准备
1. MCU 仅连接视觉模块与电脑串口。
2. 断开电机电源（避免机械动作干扰观测）。
3. 打开 VOFA+，连接调试串口（按当前工程串口映射配置）。
4. 在 VOFA 中添加 4 通道波形，顺序对应上文定义。

## 4. 测试流程
### 4.1 启动
1. 烧录并上电。
2. 在 OLED 菜单进入 DEBUG -> Vision_data。
3. 确认 VOFA 出现 4 路实时波形。

### 4.2 场景执行
按顺序执行以下动作，每个动作建议持续 10s 以上：

1. 静止目标：目标物体保持基本不动。
2. 慢速晃动：目标缓慢左右和上下移动。
3. 快速晃动：目标快速摆动，观察误差突变情况。
4. 突然移出视野：目标瞬间离开画面。
5. 重新进入视野：目标再次进入画面并稳定跟踪。

### 4.3 记录
每个场景至少保留 1 张波形截图，截图应包含：
- pixel_err_X
- pixel_err_Y
- frame_dt_ms
- status

## 5. 验收标准
以下条目全部满足可判定 Vision_data 验收通过：

1. 菜单与任务切换正常
- 能稳定进入/退出 Vision_data。
- 退出后 VOFA profile 被清空，不污染其他 DEBUG 任务。

2. 数据连续性
- pixel_err_X/pixel_err_Y 无持续性断崖尖峰毛刺。
- frame_dt_ms 随真实视觉帧率变化，不是长期固定假值。

3. 丢失目标策略正确
- status 进入 LOST_TARGET 时，pixel_err_X/pixel_err_Y 归零。
- 目标重新进入视野后，status 回到 TARGET，误差恢复正常输出。

4. 异常可定位
- 若出现偶发跳最大值或异常尖峰，可从波形定位场景与时间点。
- 该类异常应在报告中列为整改项，评估是否引入滤波（中值或低通）。

## 6. 验收报告模板
可直接复制以下模板作为本次测试记录。

### 6.1 基本信息
| 字段 | 内容 |
|---|---|
| 测试日期 |  |
| 测试人员 |  |
| 固件版本/提交号 |  |
| 硬件版本 |  |
| 上位机工具版本 |  |
| 测试环境备注 |  |

### 6.2 接线与配置确认
| 检查项 | 结果(PASS/FAIL) | 备注 |
|---|---|---|
| 已断开电机电源 |  |  |
| 仅保留视觉模块+调试串口 |  |  |
| 已进入 DEBUG -> Vision_data |  |  |
| VOFA 4通道显示正常 |  |  |

### 6.3 场景结果
| 场景 | status表现 | err_x/err_y表现 | frame_dt_ms表现 | 结论(PASS/FAIL) | 证据 |
|---|---|---|---|---|---|
| 静止目标 |  |  |  |  | 截图编号 |
| 慢速晃动 |  |  |  |  | 截图编号 |
| 快速晃动 |  |  |  |  | 截图编号 |
| 突然移出视野 |  |  |  |  | 截图编号 |
| 重新进入视野 |  |  |  |  | 截图编号 |

### 6.4 验收结论
| 验收项 | 结果(PASS/FAIL) | 备注 |
|---|---|---|
| 菜单与切换正常 |  |  |
| 数据连续性达标 |  |  |
| 丢失策略正确 |  |  |
| 异常可定位 |  |  |

总体结论：通过 / 不通过

### 6.5 问题与整改
| 问题编号 | 现象 | 触发场景 | 严重度 | 处理建议 | 负责人 | 截止日期 |
|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |

## 7. 附录：DEBUG_Smooth 入口
本模块还包含速度模式机械平顺性测试入口：

- 菜单路径：DEBUG -> DEBUG_Smooth
- 后台运行项：RUN_ENTRY_DEBUG_MOTOR_SMOOTHNESS_SPEEDMODE_VELACC
- 主要用于电机侧速度波形与加速度参数验证（不属于本次纯视觉验收范围）
