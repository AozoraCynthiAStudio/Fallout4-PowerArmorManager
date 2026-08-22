ScriptName PowerArmorMgr Extends Quest

Bool Property bForceCompile = True Auto

Int Function _MaxSlots() Global
    Return 50
EndFunction

Float Function _MenuOpenCooldownSeconds() Global
    Return 0.8
EndFunction

Float Function _ActionCooldownSeconds() Global
    Return 0.5
EndFunction

Float Function _MenuPollSeconds() Global
    Return 0.05
EndFunction

Int Function _TargetScanAttempts() Global
    Return 15
EndFunction

Function _Result(String asKind, String asTitle, String asBody, Bool abError) Global
    If !PANative.ShowResult(asKind, asTitle, asBody, abError)
        Debug.MessageBox(asTitle + "\n" + asBody)
    EndIf
EndFunction

ObjectReference Function _FindPowerArmorToStore() Global
    ObjectReference akRef = GardenOfEden3.GetCameraTargetReference()
    If _IsPowerArmorReference(akRef)
        Return akRef
    EndIf
    Return None
EndFunction

Bool Function _IsPowerArmorReference(ObjectReference akRef) Global
    If akRef == None
        Return False
    EndIf
    If PANative.IsPowerArmorReference(akRef)
        Return True
    EndIf
    Return False
EndFunction

String Function _SlotText(Int aiSlot) Global
    If PANative.HasSlotData(aiSlot)
        Return "Slot " + aiSlot + "\n" + PANative.GetSavedOutfitSummary(aiSlot)
    EndIf
    Return "Slot " + aiSlot + "\nEmpty slot"
EndFunction

Function _StoreSlot(Int aiSlot) Global
    If !PANative.BeginMenuAction(Utility.GetCurrentRealTime(), _ActionCooldownSeconds())
        Debug.Notification("Please wait a moment.")
        Return
    EndIf
    If aiSlot < 1 || aiSlot > _MaxSlots()
        aiSlot = PANative.GetCurrentSlot()
    EndIf
    ObjectReference akFrame = _FindPowerArmorToStore()
    _StoreReferenceSlot(aiSlot, akFrame, 4)
EndFunction

Function _StoreReferenceSlot(Int aiSlot, ObjectReference akFrame, Int aiSuccessVoiceEvent) Global
    If akFrame == None
        _Result("warning", "No power armor available", "Aim at a power armor and try again.", True)
        Return
    EndIf
    If !PANative.IsPowerArmorReference(akFrame)
        _Result("warning", "Store failed", "The target is not a storable power armor.", True)
        Return
    EndIf
    _PlayTransferOutEffect(akFrame)
    Utility.Wait(0.30)
    Int iCount = PANative.SavePowerArmor(aiSlot, akFrame)
    If iCount == -2
        _Result("warning", "Store failed", "This slot already contains power armor and cannot be overwritten. Deploy it first.", True)
        Return
    ElseIf iCount == -3
        _Result("warning", "Store failed", "This power armor is currently being used by another actor.", True)
        Return
    ElseIf iCount == -4
        _Result("warning", "Store failed", "Only unowned power armor or power armor owned by the player can be stored.", True)
        Return
    ElseIf iCount == -5
        _Result("warning", "Store failed", "Power armor cannot be stored during combat.", True)
        Return
    ElseIf iCount == -6
        _Result("warning", "Store failed", "This power armor is already stored in another slot.", True)
        Return
    ElseIf iCount <= 0
        _Result("warning", "Store failed", "The power armor could not be safely recorded and hidden. Its world model was not removed.", True)
        Return
    EndIf
    _StopStoreTargetEffect(akFrame)
    PANative.ClearMenuTarget()
    PANative.PlayVoiceEvent(aiSuccessVoiceEvent)
    _Result("success", "Power armor stored", "Slot: " + aiSlot + "\n\n" + PANative.GetSavedOutfitSummary(aiSlot), False)
EndFunction

Function _RecallPrimaryPowerArmor() Global
    Bool bEffectStarted = PANative.PlayPrimaryRecallEffect()
    If bEffectStarted
        Utility.Wait(0.30)
    EndIf
    Int iCount = PANative.RecallPrimaryPowerArmor()
    If iCount == -7
        Debug.Notification("No Primary Power Armor is bound. Store a frame in slot 01 through the storage UI first.")
        Return
    ElseIf iCount == -2
        Debug.Notification("Primary Power Armor recall failed: slot 01 is occupied.")
        Return
    ElseIf iCount == -3
        Debug.Notification("The Primary Power Armor is occupied and cannot be recalled.")
        Return
    ElseIf iCount == -4
        Debug.Notification("The Primary Power Armor ownership check failed.")
        Return
    ElseIf iCount == -5
        Debug.Notification("Primary Power Armor cannot be recalled during combat.")
        Return
    ElseIf iCount == -6
        Debug.Notification("The Primary Power Armor is already stored in another slot.")
        Return
    ElseIf iCount <= 0
        Debug.Notification("Primary Power Armor recall failed. Its binding and world reference were preserved.")
        Return
    EndIf

    PANative.PlayVoiceEvent(6)
    If PANative.AreOutfitNotificationsEnabled()
        Debug.Notification("Primary Power Armor recalled.")
    EndIf
EndFunction

; The primary hotkey owns this effect path. Keep it separate from the UI
; transfer helpers so changing hotkey recall cannot alter the UI workflow.
Function _PlayPrimaryHotkeyRecallEffect() Global
    ObjectReference akFrame = PANative.GetPrimaryPowerArmorRef()
    If akFrame == None
        Return
    EndIf
    EffectShader OutShader = Game.GetForm(0x000E178E) as EffectShader
    Explosion OutExplosion = Game.GetForm(0x00165597) as Explosion
    If OutShader != None
        OutShader.Play(akFrame, 0.90)
    EndIf
    If OutExplosion != None
        akFrame.PlaceAtMe(OutExplosion)
    EndIf
EndFunction

Function _DeploySlot(Int aiSlot) Global
    _DeploySlotInternal(aiSlot, False, 3)
EndFunction

Function _DeployPrimarySlot(Int aiSlot) Global
    ; Negative voice ids suppress the direct-hotkey result window while
    ; retaining the normal primary-deploy voice event.
    _DeploySlotInternal(aiSlot, True, -5)
EndFunction

Function _DeploySlotInternal(Int aiSlot, Bool abKeepPrimaryBinding, Int aiSuccessVoiceEvent) Global
    If PANative.IsEquipBusy()
        Debug.Notification("Power armor is being processed. Please wait.")
        Return
    EndIf
    If aiSlot < 1 || aiSlot > _MaxSlots()
        aiSlot = PANative.GetCurrentSlot()
    EndIf
    If !PANative.HasSlotData(aiSlot)
        Debug.Notification("Deploy failed: empty slot.")
        Return
    EndIf
    If !PANative.BeginEquipAction(Utility.GetCurrentRealTime(), _ActionCooldownSeconds())
        Debug.Notification("Please wait a moment.")
        Return
    EndIf
    ObjectReference akFrame = PANative.GetSavedPowerArmorRef(aiSlot)
    ObjectReference akDeployCue = _PlayDeployArrivalCue(Game.GetPlayer())
    Utility.Wait(0.35)
    Int iResult = PANative.DeployPowerArmor(aiSlot, Game.GetPlayer())
    If iResult <= 0 && iResult != -5
        ; Native reference moves can finish one frame later in a new cell.
        ; Retry once without changing the slot record or showing a false success.
        Utility.Wait(0.15)
        iResult = PANative.DeployPowerArmor(aiSlot, Game.GetPlayer())
    EndIf
    _CleanupDeployArrivalCue(akDeployCue)
    If iResult == -5
        PANative.FinishEquipAction()
        _Result("warning", "Deploy failed", "Power armor cannot be deployed during combat.", True)
        Return
    ElseIf iResult <= 0
        PANative.FinishEquipAction()
        _Result("warning", "Deploy failed", "The power armor could not be moved safely. The slot record remains intact and the power armor remains in storage.\nSlot: " + aiSlot, True)
        Return
    EndIf
    If aiSlot == 1 && !abKeepPrimaryBinding
        PANative.ClearPrimaryPowerArmorBinding()
    EndIf
    ; The saved frame is disabled before deployment. Play the proven transfer
    ; effect only after native placement has enabled and refreshed its 3D.
    Utility.Wait(0.10)
    _PlayTransferInEffect(akFrame)
    PANative.FinishEquipAction()
    Bool bShowCompletion = aiSuccessVoiceEvent >= 0
    If aiSuccessVoiceEvent < 0
        aiSuccessVoiceEvent = -aiSuccessVoiceEvent
    EndIf
    PANative.PlayVoiceEvent(aiSuccessVoiceEvent)
    If bShowCompletion && PANative.AreOutfitNotificationsEnabled()
        _Result("success", "Power armor deployed", "If the position is not suitable, move to an open area and deploy it again.", False)
    EndIf
EndFunction

ObjectReference Function _PlayDeployArrivalCue(Actor akPlayer) Global
    If akPlayer == None
        Return None
    EndIf
    Form TrailForm = Game.GetForm(0x000FBA79)
    ObjectReference akTrail = None
    If TrailForm != None
        akTrail = akPlayer.PlaceAtMe(TrailForm, 1, False, False)
        If akTrail != None
            ; Use the same world-space landing point as native deployment.
            Float fAngleZ = akPlayer.GetAngleZ()
            Float fTargetX = akPlayer.GetPositionX() + Math.Sin(fAngleZ) * 190.0
            Float fTargetY = akPlayer.GetPositionY() + Math.Cos(fAngleZ) * 190.0
            Float fTargetZ = akPlayer.GetPositionZ() + 12.0
            akTrail.SetPosition(fTargetX, fTargetY, fTargetZ)
            akTrail.SetAngle(0.0, 0.0, fAngleZ)
        EndIf
    EndIf
    Explosion TransferExplosion = Game.GetForm(0x0015FF5E) as Explosion
    If TransferExplosion != None && akTrail != None
        ; Keep the complete Institute arrival burst at the deployment marker.
        ; Never fall back to the player, which can make the effect hit the camera.
        akTrail.PlaceAtMe(TransferExplosion)
    EndIf
    Return akTrail
EndFunction

Function _CleanupDeployArrivalCue(ObjectReference akCue) Global
    If akCue != None
        akCue.Disable()
        akCue.Delete()
    EndIf
EndFunction
Function _PlayTransferOutEffect(ObjectReference akFrame) Global
    If akFrame == None
        Return
    EndIf
    EffectShader TransferShader = Game.GetForm(0x000E178E) as EffectShader
    Explosion TransferExplosion = Game.GetForm(0x00165597) as Explosion
    If TransferShader != None
        TransferShader.Play(akFrame, 0.90)
    EndIf
    If TransferExplosion != None
        akFrame.PlaceAtMe(TransferExplosion)
    EndIf
EndFunction

Function _PlayTransferInEffect(ObjectReference akFrame) Global
    If akFrame == None
        Return
    EndIf
    ; Deployment uses the vanilla teleport-in pair. Storage uses the
    ; separate teleport-out pair above.
    Bool bNativeEffect = PANative.PlayDeployEffect(akFrame)
    If !bNativeEffect
        EffectShader TransferShader = Game.GetForm(0x000F62AE) as EffectShader
        If TransferShader != None
            TransferShader.Play(akFrame, 0.90)
        EndIf
    EndIf
    Explosion TransferExplosion = Game.GetForm(0x0015FF5E) as Explosion
    If TransferExplosion != None
        akFrame.PlaceAtMe(TransferExplosion)
    EndIf
EndFunction

Function _StopTransferEffect(ObjectReference akFrame) Global
    If akFrame == None
        Return
    EndIf
    EffectShader OutShader = Game.GetForm(0x000E178E) as EffectShader
    EffectShader InShader = Game.GetForm(0x000F62AE) as EffectShader
    If OutShader != None
        OutShader.Stop(akFrame)
    EndIf
    If InShader != None
        InShader.Stop(akFrame)
    EndIf
EndFunction

Function _SelectSlot(Int aiSlot) Global
    If aiSlot < 1
        aiSlot = _MaxSlots()
    ElseIf aiSlot > _MaxSlots()
        aiSlot = 1
    EndIf
    PANative.SetCurrentSlot(aiSlot)
EndFunction

Function _StopStoreTargetEffect(ObjectReference akTarget) Global
    If akTarget != None
        EffectShader TargetShader = Game.GetForm(0x001E077C) as EffectShader
        If TargetShader != None
            TargetShader.Stop(akTarget)
        EndIf
    EndIf
EndFunction

Function _HandleMenuAction(Int aiAction, Int aiSlot, String asName, ObjectReference akStoreTarget) Global
    If aiAction == 1
        PANative.SetCurrentSlot(aiSlot)
        _StoreReferenceSlot(aiSlot, akStoreTarget, 4)
    ElseIf aiAction == 2
        PANative.SetCurrentSlot(aiSlot)
        _DeploySlot(aiSlot)
    ElseIf aiAction == 9
        If aiSlot == 1
            _Result("warning", "Rename unavailable", "Primary Power Armor is a reserved system name and cannot be changed.", True)
            Return
        EndIf
        PANative.SetSlotName(aiSlot, asName)
    ElseIf aiAction == 8
        _SelectSlot(aiSlot)
    EndIf
EndFunction

Function OnMcmMenuHotkey() Global
    If PANative.IsMenuOpen()
        ObjectReference akTarget = PANative.GetMenuStoreTarget()
        _StopStoreTargetEffect(akTarget)
        PANative.CloseMenu()
        PANative.ClearMenuTarget()
        Return
    EndIf
    Int iBlockReason = PANative.GetMenuOpenBlockReason()
    If iBlockReason == 1
        Debug.Notification("Power Armor Storage cannot open while another game menu is active.")
        Return
    ElseIf iBlockReason == 2
        Debug.Notification("Power Armor Storage cannot open while another Prisma UI is active.")
        Return
    EndIf
    If !PANative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("Menu is cooling down.")
        Return
    EndIf
    If PANative.OpenMenu(Game.GetPlayer(), None)
        ObjectReference akStoreTarget = None
        Int iGuard = 0
        Int iOpenGuard = 0
        Bool bMenuObservedOpen = False
        Bool bKeepPolling = True
        While iGuard < 18000 && bKeepPolling
            Bool bMenuOpen = PANative.IsMenuOpen()
            Int iPendingAction = PANative.GetMenuAction()
            If bMenuOpen
                bMenuObservedOpen = True
                ObjectReference akCandidate = _FindPowerArmorToStore()
                If akCandidate != akStoreTarget
                    _StopStoreTargetEffect(akStoreTarget)
                    PANative.ClearMenuTarget()
                    akStoreTarget = akCandidate
                    If akStoreTarget != None
                        PANative.SetMenuStoreTarget(akStoreTarget)
                    EndIf
                ElseIf akStoreTarget != None
                    PANative.SetMenuStoreTarget(akStoreTarget)
                EndIf
            EndIf
            Int iAction = iPendingAction
            If iAction != 0
                Int iSlot = PANative.GetMenuActionSlot()
                String sName = PANative.GetMenuActionName()
                PANative.ClearMenuAction()
                _HandleMenuAction(iAction, iSlot, sName, akStoreTarget)
                If iAction == 1 && PANative.HasSlotData(iSlot)
                    akStoreTarget = None
                EndIf
            EndIf
            If !bMenuOpen && iAction == 0
                If bMenuObservedOpen
                    bKeepPolling = False
                Else
                    iOpenGuard += 1
                    If iOpenGuard >= 50
                        bKeepPolling = False
                    EndIf
                EndIf
            EndIf
            Utility.Wait(_MenuPollSeconds())
            iGuard += 1
        EndWhile
        _StopStoreTargetEffect(akStoreTarget)
        PANative.ClearMenuTarget()
        Return
    EndIf
    PANative.ClearMenuTarget()
    iBlockReason = PANative.GetMenuOpenBlockReason()
    If iBlockReason == 1
        Debug.Notification("Power Armor Storage cannot open while another game menu is active.")
    ElseIf iBlockReason == 2
        Debug.Notification("Power Armor Storage cannot open while another Prisma UI is active.")
    Else
        Debug.MessageBox("Power Armor Storage UI is not connected.\nPlease make sure Prisma UI Framework is installed.")
    EndIf
EndFunction

Function OnMcmSaveHotkey() Global
    _StoreSlot(1)
EndFunction

Function OnMcmLoadHotkey() Global
    _DeploySlot(1)
EndFunction

Function OnMcmPrimaryArmorHotkey() Global
    If PANative.HasSlotData(1)
        _DeployPrimarySlot(1)
        Return
    EndIf
    _RecallPrimaryPowerArmor()
EndFunction

Function OnMcmNextOutfitHotkey() Global
    _SelectSlot(PANative.GetCurrentSlot() + 1)
    Debug.MessageBox(_SlotText(PANative.GetCurrentSlot()))
EndFunction

Function OnMcmPreviousOutfitHotkey() Global
    _SelectSlot(PANative.GetCurrentSlot() - 1)
    Debug.MessageBox(_SlotText(PANative.GetCurrentSlot()))
EndFunction

Function OnMcmRandomHotkey() Global
    Debug.Notification("Power Armor Storage does not use random functions.")
EndFunction

Function OnMcmCycleSlotHotkey() Global
    OnMcmNextOutfitHotkey()
EndFunction



