ScriptName PAM3Hotkeys Hidden

String Function DescribeResult(Int aiCode) Global
    If aiCode == 1
        Return "操作完成，请保存并读档后复测。"
    ElseIf aiCode == 0
        Return "找不到动力甲引用。仓位记录未删除；请勿覆盖测试存档。"
    ElseIf aiCode == -1
        Return "目标或槽位无效。"
    ElseIf aiCode == -2
        Return "槽位已有记录，不允许覆盖。"
    ElseIf aiCode == -3
        Return "动力甲正在被使用。"
    ElseIf aiCode == -4
        Return "该动力甲不属于玩家或无主状态。"
    ElseIf aiCode == -5
        Return "战斗期间不允许存取动力甲。"
    ElseIf aiCode == -6
        Return "该动力甲已存入另一个槽位。"
    ElseIf aiCode == -7
        Return "主动力甲尚未绑定。"
    ElseIf aiCode == -8
        Return "上一笔存取尚未结束。"
    ElseIf aiCode == -9
        Return "动力甲世界状态与仓位记录冲突，未自动覆盖。"
    ElseIf aiCode == -10
        Return "引擎传送/隐藏失败。仓位数据仍保留。"
    ElseIf aiCode == -11
        Return "旧存档的动力甲 FormID 未能安全映射。仓位仍保留。"
    ElseIf aiCode == -13
        Return "检测到损坏的仓位记录。已锁定存取，请勿覆盖原始存档。"
    ElseIf aiCode == -12
        Return "01 主动力甲仍有绑定，请先测试解除绑定功能。"
    EndIf
    Return "未知错误代码：" + aiCode
EndFunction

Function OnStore() Global
    Int slot = PAM3Native.GetCurrentSlot()
    ObjectReference frame = GardenOfEden3.GetCameraTargetReference()
    If frame == None
        Debug.Notification("没有准星目标：请对准无人的动力甲骨架。")
        Return
    EndIf
    Int result = PAM3Native.StorePowerArmor(slot, frame)
    Debug.Notification("[" + slot + "] 存放：" + DescribeResult(result))
EndFunction

Function OnDeploy() Global
    Int slot = PAM3Native.GetCurrentSlot()
    Int result = PAM3Native.DeployPowerArmor(slot, Game.GetPlayer())
    Debug.Notification("[" + slot + "] 部署：" + DescribeResult(result))
EndFunction

Function OnPrimary() Global
    Int result = PAM3Native.TogglePrimary()
    Debug.Notification("[01] 主动力甲：" + DescribeResult(result))
EndFunction

Function OnNextSlot() Global
    Int slot = PAM3Native.GetCurrentSlot() + 1
    If slot > 50
        slot = 1
    EndIf
    PAM3Native.SetCurrentSlot(slot)
    OnInspect()
EndFunction

Function OnPreviousSlot() Global
    Int slot = PAM3Native.GetCurrentSlot() - 1
    If slot < 1
        slot = 50
    EndIf
    PAM3Native.SetCurrentSlot(slot)
    OnInspect()
EndFunction

Function OnInspect() Global
    Int health = PAM3Native.GetLoadHealth()
    If health == -1
        Debug.MessageBox("动力甲仓库检测到损坏/不兼容的存档记录，已保护原始数据。请勿覆盖原始存档；请向开发者报告。")
        Return
    EndIf
    Int slot = PAM3Native.GetCurrentSlot()
    Int status = PAM3Native.GetSlotStatus(slot)
    If status == 0
        Debug.Notification("仓位 " + slot + "：空")
    ElseIf status == 1
        Debug.Notification("仓位 " + slot + "：已存，引用有效")
    ElseIf status == 2
        Debug.Notification("仓位 " + slot + "：记录仍在，但动力甲引用不可用")
    ElseIf status == 3
        Debug.Notification("仓位 " + slot + "：记录与世界状态冲突，禁止自动部署")
    Else
        Debug.Notification("仓位状态错误：" + status)
    EndIf
EndFunction

; Never call ClearPrimaryBinding automatically on a failed recall.
; This explicit action is intentionally absent from the MCM hotkeys.
Function OnClearPrimaryBinding() Global
    Bool cleared = PAM3Native.ClearPrimaryBinding()
    If cleared
        Debug.Notification("01 主动力甲绑定已清除；仓位中的动力甲不会被删除。")
    Else
        Debug.Notification("01 槽位仍占用，拒绝清除绑定。")
    EndIf
EndFunction
