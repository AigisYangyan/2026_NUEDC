<#
.SYNOPSIS
  AGENTS.md §4 依赖矩阵扫描器 —— 检测跨层非法 #include。

.DESCRIPTION
  唯一的层规则实现。基线生成与违规检查共用本文件，因此两者不可能漂移。

  层规则来源（AGENTS.md）：
    §3.2 Driver      不得包含 Middleware / App
    §3.3 Middleware  不得包含 Driver / DL HAL / App
    §3.5 Task        不得包含 Driver / Middleware / DL HAL（只能调 Service）
    §3.6 scheduler   同 Task；ui 同 Task；system 不得包含 DL HAL（可经 Driver 接口装配）
    §3.4 Service     不得包含 DL HAL
    §3.7 Utils       不得依赖 Driver / Middleware / App

  §11 存量债务：app/ 层现有 50 处违规是待重置的债，不是缺陷。基线记录它们，
  使本扫描器只对「新增」违规报警 —— 即 §11 第 1 条「新代码不得增加或复制上述违规模式」。

.PARAMETER Mode
  baseline : 扫描全仓库，把当前违规写入基线文件（仅在用户明确要求时执行）
  check    : 扫描全仓库，报告不在基线内的新增违规
  file     : 只扫描 -Path 指定的单个文件，报告新增违规

.OUTPUTS
  违规行写入 stdout，格式 `<相对路径>|<被包含的非法头>`。无违规则无输出。
  退出码恒为 0；判定交给调用方（hook）。
#>
param(
    [ValidateSet('baseline', 'check', 'file')]
    [string]$Mode = 'check',
    [string]$Path
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BaselineFile = Join-Path $PSScriptRoot 'arch-baseline.txt'

# DL HAL 识别模式（§3.1）
$DlHal = '(^|/)ti/driverlib/|(^|/)?ti_msp_dl_config\.h'

# 每条规则：路径前缀 -> 该层禁止包含的模式。首个匹配的前缀生效，故顺序由窄到宽。
$LayerRules = @(
    @{ Prefix = 'hc-team/driver/';         Forbid = @('(^|/)middleware/', '(^|/)app/', '(^|/)hc-team/app/') }
    @{ Prefix = 'hc-team/middleware/';     Forbid = @($DlHal, '(^|/)driver/', '(^|/)app/') }
    @{ Prefix = 'hc-team/utils/';          Forbid = @($DlHal, '(^|/)driver/', '(^|/)middleware/', '(^|/)app/') }
    @{ Prefix = 'hc-team/app/service/';    Forbid = @($DlHal) }
    @{ Prefix = 'hc-team/app/system/';     Forbid = @($DlHal) }
    @{ Prefix = 'hc-team/app/tasks/';      Forbid = @($DlHal, '(^|/)driver/', '(^|/)middleware/') }
    @{ Prefix = 'hc-team/app/scheduler/';  Forbid = @($DlHal, '(^|/)driver/', '(^|/)middleware/') }
    @{ Prefix = 'hc-team/app/ui/';         Forbid = @($DlHal, '(^|/)driver/', '(^|/)middleware/') }
)

function Get-Violations {
    param([string]$AbsPath)

    if (-not (Test-Path -LiteralPath $AbsPath -PathType Leaf)) { return @() }

    $rel = [System.IO.Path]::GetRelativePath($RepoRoot, $AbsPath).Replace('\', '/')
    $rule = $LayerRules | Where-Object { $rel.StartsWith($_.Prefix) } | Select-Object -First 1
    if (-not $rule) { return @() }

    $found = @()
    foreach ($line in (Get-Content -LiteralPath $AbsPath -ErrorAction SilentlyContinue)) {
        # 只取 #include 的头路径；行内注释与尾随空白不参与判定
        if ($line -notmatch '^\s*#\s*include\s*[<"]([^>"]+)[>"]') { continue }
        $inc = $Matches[1]
        foreach ($pat in $rule.Forbid) {
            if ($inc -match $pat) { $found += "$rel|$inc"; break }
        }
    }
    return $found
}

function Get-AllViolations {
    $files = Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'hc-team') -Recurse -Include '*.c', '*.h' -File -ErrorAction SilentlyContinue
    $all = @()
    foreach ($f in $files) { $all += Get-Violations -AbsPath $f.FullName }
    return $all
}

function Get-Baseline {
    if (-not (Test-Path -LiteralPath $BaselineFile)) { return @() }
    return @(Get-Content -LiteralPath $BaselineFile | Where-Object { $_ -and -not $_.StartsWith('#') })
}

switch ($Mode) {
    'baseline' {
        $v = Get-AllViolations | Sort-Object -Unique
        $header = @(
            '# AGENTS.md §11 存量架构债务基线 —— 由 .claude/hooks/arch-scan.ps1 -Mode baseline 生成',
            '# 格式: <相对路径>|<被包含的非法头>',
            '#',
            '# 这些是「待重置的债」，不是新代码可采用的范例（§11）。',
            '# arch-guard 只对不在本表中的「新增」违规报警。',
            '# 修复一条违规后，把对应行从本表删除，使它不可能被悄悄重新引入。',
            ''
        )
        Set-Content -LiteralPath $BaselineFile -Value ($header + $v) -Encoding utf8
        Write-Host "基线已写入: $BaselineFile ($($v.Count) 条存量违规)"
    }
    'check' {
        $baseline = Get-Baseline
        Get-AllViolations | Sort-Object -Unique | Where-Object { $baseline -notcontains $_ }
    }
    'file' {
        if (-not $Path) { exit 0 }
        $baseline = Get-Baseline
        Get-Violations -AbsPath $Path | Sort-Object -Unique | Where-Object { $baseline -notcontains $_ }
    }
}
