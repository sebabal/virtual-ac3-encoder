# Stop the Virtual AC3 Encoder engine and its autostart supervisor (matched by command line).
# Used by the uninstaller; safe to run anytime.
Get-CimInstance Win32_Process -Filter "Name='engine.exe' OR Name='wscript.exe'" |
  Where-Object { $_.CommandLine -like '*Virtual AC3 Encoder*' } |
  ForEach-Object { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null }
