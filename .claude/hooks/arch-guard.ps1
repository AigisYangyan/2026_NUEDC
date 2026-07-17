<#
.SYNOPSIS
  PostToolUse hook —— 文件写入后立即检查是否新增跨层违规（AGENTS.md §4）。

.DESCRIPTION
  快速反馈层。PostToolUse 无法阻止写入本身（文件已落盘），但 exit 2 会把 stderr
  直接喂回模型，使违规在当轮内就被指出。Stop 层（topo-guard.ps1）是真正的闸门。

  判定逻辑全部委托给 arch-scan.ps1，本文件不重复实现任何层规则。
#>
$ErrorActionPreference = 'Stop'

try {
    $payload = ($input | Out-String)
    if (-not $payload.Trim()) { exit 0 }
    $data = $payload | ConvertFrom-Json

    $file = $data.tool_input.file_path
    if (-not $file) { exit 0 }
    if ($file -notmatch '\.(c|h)$') { exit 0 }

    $violations = & (Join-Path $PSScriptRoot 'arch-scan.ps1') -Mode file -Path $file
    if (-not $violations) { exit 0 }

    $msg = @(
        "[架构闸门] AGENTS.md §4 依赖矩阵违规 —— 本次编辑新增了跨层 #include:",
        ""
    )
    foreach ($v in $violations) {
        $parts = $v -split '\|', 2
        $msg += "  $($parts[0])  ->  #include `"$($parts[1])`""
    }
    $msg += @(
        "",
        "这不在 §11 存量债务基线内，属于新增违规。§11 第 1 条：新代码不得增加或复制违规模式。",
        "必须修正依赖方向后再继续，不得保留。若该依赖确实无法避免，按 §12 停止并报告架构冲突，",
        "给出冲突点、违反的层级规则，以及至少一个保持单向依赖的替代方案。"
    )

    [Console]::Error.WriteLine($msg -join "`n")
    exit 2
}
catch {
    # hook 自身故障不得阻断正常工作流；出错时放行并留痕
    [Console]::Error.WriteLine("[架构闸门] hook 内部错误(已放行): $_")
    exit 0
}
