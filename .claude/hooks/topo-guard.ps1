<#
.SYNOPSIS
  Stop hook —— 收工闸门。把 AGENTS.md §14（拓扑同步）和 §4（依赖矩阵）
  从「模型必须记得的规则」降级为「harness 的断言」。

.DESCRIPTION
  两项断言：
    A. 无新增跨层违规（§4 / §11 第 1 条）—— 判定委托 arch-scan.ps1
    B. 动了 hc-team/**/*.{c,h} 就必须动拓扑（§14 第 3、4 条）

  关于 B 的「误报」：§14 第 4 条明确要求「即使 API 和依赖没有变化，也必须记录
  『已复核，无拓扑变化』」。因此改码未碰拓扑一律是违规，不存在合法例外。

  循环保护：Claude Code 在因 Stop hook 而继续时会置 stop_hook_active=true。
  此时放行，确保闸门最多强制一轮修正，不会把会话卡死在无法满足的断言上。
#>
$ErrorActionPreference = 'Stop'

$TopologyPath = 'agent/api_architecture_topology.md'

try {
    $payload = ($input | Out-String)
    $data = if ($payload.Trim()) { $payload | ConvertFrom-Json } else { $null }

    # 循环保护：已经因本 hook 继续过一轮，不再阻断
    if ($data -and $data.stop_hook_active) { exit 0 }

    $repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    Push-Location $repo
    try {
        $status = @(git status --porcelain 2>$null)
    }
    finally {
        Pop-Location
    }

    $problems = @()

    # --- 断言 A: 新增跨层违规 ---
    $violations = & (Join-Path $PSScriptRoot 'arch-scan.ps1') -Mode check
    if ($violations) {
        $lines = foreach ($v in $violations) {
            $p = $v -split '\|', 2
            "    $($p[0])  ->  #include `"$($p[1])`""
        }
        $problems += @(
            "[§4 依赖矩阵] 存在不在 §11 基线内的新增跨层违规:",
            ($lines -join "`n"),
            "  修正依赖方向；或按 §12 停止并报告架构冲突 + 一个保持单向依赖的替代方案。"
        )
    }

    # --- 断言 B: 拓扑同步 ---
    # git status --porcelain 的路径从第 4 列开始；重命名形如 `R  old -> new`
    $paths = $status | ForEach-Object {
        $p = $_.Substring(3)
        if ($p -match '\s->\s(.+)$') { $Matches[1] } else { $p }
    } | ForEach-Object { $_.Trim('"') }

    $codeTouched = @($paths | Where-Object { $_ -match '^hc-team/.*\.(c|h)$' })
    $topoTouched = @($paths | Where-Object { $_ -eq $TopologyPath -or $_ -like 'agent/topology/*' })

    if ($codeTouched.Count -gt 0 -and $topoTouched.Count -eq 0) {
        $shown = $codeTouched | Select-Object -First 8
        $more = if ($codeTouched.Count -gt 8) { "    ... 另有 $($codeTouched.Count - 8) 个文件" } else { $null }
        $problems += @(
            "[§14 拓扑同步] 以下源文件已改动，但 $TopologyPath 未更新:",
            (($shown | ForEach-Object { "    $_" }) -join "`n"),
            $more,
            "  §14 第 3 条：编码后必须同步类图、API、依赖箭头、数据处理位置、资源所有权、交叉风险登记。",
            "  §14 第 4 条：即使 API 和依赖没有变化，也必须在更新日志记录『已复核，无拓扑变化』。",
            "  §14：代码已修改但拓扑未复核和更新时，任务不得标记为完成。",
            "  可调用 topo-updater 子 agent 完成本项，避免把 56KB 拓扑拉进主上下文。"
        ) | Where-Object { $_ }
    }

    if ($problems.Count -eq 0) { exit 0 }

    [Console]::Error.WriteLine((@("收工闸门未通过 —— 以下项必须先处理:", "") + $problems) -join "`n")
    exit 2
}
catch {
    [Console]::Error.WriteLine("[收工闸门] hook 内部错误(已放行): $_")
    exit 0
}
