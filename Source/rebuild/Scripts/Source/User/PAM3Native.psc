ScriptName PAM3Native Hidden

; 1 = successful world transition; 0 = reference not currently resolvable.
; negative failure codes are documented in Source/rebuild/README.md.
Int Function StorePowerArmor(Int aiSlot, ObjectReference akFrame) Global Native
Int Function DeployPowerArmor(Int aiSlot, Actor akPlayer) Global Native
Int Function TogglePrimary() Global Native
; 0=empty, 1=stored and resolvable, 2=missing/unresolved, 3=world-state conflict.
Int Function GetSlotStatus(Int aiSlot) Global Native
Int Function GetOccupiedCount() Global Native
; -1=unreadable record quarantined, 0=new, 1=legacy imported, 2=v2 loaded.
Int Function GetLoadHealth() Global Native
Int Function GetCurrentSlot() Global Native
Function SetCurrentSlot(Int aiSlot) Global Native
Bool Function ClearPrimaryBinding() Global Native
