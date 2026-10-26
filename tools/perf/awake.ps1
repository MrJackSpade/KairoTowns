# Dot-sourced by the device perf scripts after they define Adb: runs a block with the device held
# awake. The RG DS suspends during long runs, freezing them, unless a wake lock is held (root).
function Invoke-Awake([scriptblock]$Body) {
    Adb shell "su 0 sh -c 'echo kairotowns-perf > /sys/power/wake_lock'" | Out-Null
    try { & $Body }
    finally { Adb shell "su 0 sh -c 'echo kairotowns-perf > /sys/power/wake_unlock'" | Out-Null }
}
