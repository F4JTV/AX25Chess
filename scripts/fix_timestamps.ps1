# fix_timestamps.ps1 - bring the project's files dated in the future back to now.
#
# Ninja rebuilds build.ninja whenever one of its inputs (CMakeLists.txt, the
# .cmake files) is newer than it. An input dated later than the computer's
# clock stays newer than every build.ninja CMake writes, and the build loops
# on "Re-running CMake..." until Ninja gives up after 100 tries. The dates
# in the archive are right; the clock is not - typically Windows reading the
# hardware clock as local time on a PC that also runs Linux, which keeps it
# in UTC, so Windows runs two hours behind until it next synchronises.
#
# Called by build_all.bat. Changes nothing but the date of such files.
#
# This file is part of AX25Chess.
# SPDX-License-Identifier: GPL-2.0-or-later

param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$now = Get-Date
$limit = $now.AddMinutes(2)
$future = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $limit -and $_.FullName -notmatch '\\(build[^\\]*|\.git)\\' })

if ($future.Count -gt 0) {
    $newest = $future | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Write-Output ("[--] {0} file(s) dated after this computer's clock: the newest, {1}, at {2:yyyy-MM-dd HH:mm}; the clock says {3:yyyy-MM-dd HH:mm}." -f $future.Count, $newest.Name, $newest.LastWriteTime, $now)
    Write-Output "     Ninja would re-run CMake without end; their date is set to now."
    Write-Output "     Check the Windows clock: on a PC that also runs Linux it is often two hours behind."
    foreach ($f in $future) { $f.LastWriteTime = $now }
} else {
    Write-Output "[ok] Dates      no file dated after the clock"
}
exit 0
