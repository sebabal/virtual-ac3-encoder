' Virtual AC3 Encoder autostart supervisor.
' Runs the engine hidden and restarts it if it exits. Self-locating: it derives the engine
' path from its own folder, so it works wherever the app is installed.
Set fso = CreateObject("Scripting.FileSystemObject")
base = fso.GetParentFolderName(WScript.ScriptFullName)
appPath = base & "\engine.exe"
logFile = base & "\engine.log"
Set sh = CreateObject("WScript.Shell")
q = Chr(34)
Do
  sh.Run q & appPath & q & " --hidden --log " & q & logFile & q, 0, True
  WScript.Sleep 5000
Loop
