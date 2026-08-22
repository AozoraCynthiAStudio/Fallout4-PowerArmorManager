ScriptName PAControllerQuestScript Extends Quest

Form Property ControllerItem Auto

Event OnInit()
    GiveController()
    StartTimer(10.0, 1)
EndEvent

Function GiveController()
    Actor akPlayer = Game.GetPlayer()
    If akPlayer != None && ControllerItem != None && akPlayer.GetItemCount(ControllerItem) <= 0
        akPlayer.AddItem(ControllerItem, 1, True)
    EndIf
EndFunction

Event OnTimer(Int aiTimerID)
    If aiTimerID == 1
        GiveController()
        StartTimer(30.0, 1)
    EndIf
EndEvent
