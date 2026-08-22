ScriptName PAControllerEffectScript Extends ActiveMagicEffect

Form Property ControllerItem Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    ; Pip-Boy can keep input focus, so close it completely before opening storage.
    If UI.IsMenuOpen("PipboyMenu")
        UI.CloseMenu("PipboyMenu")
        Int iGuard = 0
        While iGuard < 20 && UI.IsMenuOpen("PipboyMenu")
            Utility.Wait(0.05)
            iGuard += 1
        EndWhile
    EndIf
    Utility.Wait(0.10)
    PowerArmorMgr.OnMcmMenuHotkey()

    Actor akPlayer = Game.GetPlayer()
    If akPlayer != None && ControllerItem != None && akPlayer.GetItemCount(ControllerItem) <= 0
        akPlayer.AddItem(ControllerItem, 1, True)
    EndIf
EndEvent
