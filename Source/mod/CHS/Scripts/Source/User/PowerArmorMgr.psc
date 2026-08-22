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
    Return "槽位 " + aiSlot + "\n" + PANative.GetSavedOutfitSummary(aiSlot)
    EndIf
    Return "槽位 " + aiSlot + "\n空槽位"
EndFunction

Function _StoreSlot(Int aiSlot) Global
    If !PANative.BeginMenuAction(Utility.GetCurrentRealTime(), _ActionCooldownSeconds())
        Debug.Notification("请稍候。")
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
        _Result("warning", "没有可保存的动力甲", "请将准星对准动力甲后再操作。", True)
        Return
    EndIf
    If !PANative.IsPowerArmorReference(akFrame)
        _Result("warning", "存放失败", "目标不是可存放的动力甲。", True)
        Return
    EndIf
    _PlayTransferOutEffect(akFrame)
    Utility.Wait(0.30)
    Int iCount = PANative.SavePowerArmor(aiSlot, akFrame)
    If iCount == -2
        _Result("warning", "存放失败", "此槽位已有动力甲，不支持覆盖保存。请先部署。", True)
        Return
    ElseIf iCount == -3
        _Result("warning", "存放失败", "这台动力甲正在被其他人使用。", True)
        Return
    ElseIf iCount == -4
        _Result("warning", "存放失败", "只能存放无主或属于玩家的动力甲。", True)
        Return
    ElseIf iCount == -5
        _Result("warning", "存放失败", "战斗中不能存放动力甲。", True)
        Return
    ElseIf iCount == -6
        _Result("warning", "存放失败", "这台动力甲已经存放在其他槽位。", True)
        Return
    ElseIf iCount <= 0
        _Result("warning", "存放失败", "动力甲无法被安全记录并隐藏，世界中的模型未被移除。", True)
        Return
    EndIf
    _StopStoreTargetEffect(akFrame)
    PANative.ClearMenuTarget()
    PANative.PlayVoiceEvent(aiSuccessVoiceEvent)
    _Result("success", "动力甲已存放", "槽位：" + aiSlot + "\n\n" + PANative.GetSavedOutfitSummary(aiSlot), False)
EndFunction

Function _RecallPrimaryPowerArmor() Global
    Bool bEffectStarted = PANative.PlayPrimaryRecallEffect()
    If bEffectStarted
        Utility.Wait(0.30)
    EndIf
    Int iCount = PANative.RecallPrimaryPowerArmor()
    If iCount == -7
        Debug.Notification("尚未绑定主动力甲。请先通过仓库界面把动力甲存入 01 槽位。")
        Return
    ElseIf iCount == -2
        Debug.Notification("主动力甲收回失败：01 槽位已被占用。")
        Return
    ElseIf iCount == -3
        Debug.Notification("主动力甲正在被其他人使用，无法收回。")
        Return
    ElseIf iCount == -4
        Debug.Notification("主动力甲的所有权不允许收回。")
        Return
    ElseIf iCount == -5
        Debug.Notification("战斗中不能收回主动力甲。")
        Return
    ElseIf iCount == -6
        Debug.Notification("主动力甲已经存放在其他槽位。")
        Return
    ElseIf iCount <= 0
        Debug.Notification("主动力甲收回失败，绑定和世界模型均已保留。")
        Return
    EndIf

    PANative.PlayVoiceEvent(6)
    If PANative.AreOutfitNotificationsEnabled()
        Debug.Notification("主动力甲已收回。")
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
        Debug.Notification("动力甲正在处理，请稍候。")
        Return
    EndIf
    If aiSlot < 1 || aiSlot > _MaxSlots()
        aiSlot = PANative.GetCurrentSlot()
    EndIf
    If !PANative.HasSlotData(aiSlot)
        Debug.Notification("部署失败：槽位为空。")
        Return
    EndIf
    If !PANative.BeginEquipAction(Utility.GetCurrentRealTime(), _ActionCooldownSeconds())
        Debug.Notification("请稍候。")
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
        _Result("warning", "部署失败", "战斗中不能部署动力甲。", True)
        Return
    ElseIf iResult <= 0
        PANative.FinishEquipAction()
        _Result("warning", "部署失败", "动力甲无法安全移动。槽位记录保留，动力甲仍在仓库中。\n槽位：" + aiSlot, True)
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
        _Result("success", "动力甲已部署", "如果位置不合适，请换到开阔位置后重新部署。", False)
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
            _Result("warning", "无法命名", "“主动力甲”是系统保留名称，不允许修改。", True)
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
        Debug.Notification("其他游戏菜单已打开，无法打开动力甲仓库。")
        Return
    ElseIf iBlockReason == 2
        Debug.Notification("其他 Prisma UI 已打开，无法打开动力甲仓库。")
        Return
    EndIf
    If !PANative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("菜单正在冷却，请稍候。")
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
        Debug.Notification("其他游戏菜单已打开，无法打开动力甲仓库。")
    ElseIf iBlockReason == 2
        Debug.Notification("其他 Prisma UI 已打开，无法打開动力甲仓库。")
    Else
        Debug.MessageBox("动力甲仓库 UI 未连接。\n请确认已安装 Prisma UI Framework。")
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
    Debug.Notification("动力甲仓库不使用随机功能。")
EndFunction

Function OnMcmCycleSlotHotkey() Global
    OnMcmNextOutfitHotkey()
EndFunction



