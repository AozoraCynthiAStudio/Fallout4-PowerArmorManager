ScriptName PANative Hidden

String Function GetPluginVersion() Global Native
String Function GetSlotPath(Int aiSlot) Global Native
Bool Function IsMenuAvailable() Global Native
Int Function GetMenuOpenBlockReason() Global Native
Bool Function OpenMenu(Actor akTarget, ObjectReference akStoreTarget) Global Native
Function CloseMenu() Global Native
Bool Function ShowResult(String asKind, String asTitle, String asBody, Bool abError) Global Native
Bool Function IsMenuOpen() Global Native
Function PlayVoiceEvent(Int aiEvent) Global Native
Actor Function GetMenuTarget() Global Native
ObjectReference Function GetMenuStoreTarget() Global Native
Function SetMenuStoreTarget(ObjectReference akTarget) Global Native
Function ClearMenuTarget() Global Native
Float Function GetMenuOpenCooldownRemaining(Float afNow) Global Native
Bool Function BeginMenuOpen(Float afNow, Float afCooldownSeconds) Global Native
Float Function GetMenuActionCooldownRemaining(Float afNow) Global Native
Bool Function BeginMenuAction(Float afNow, Float afCooldownSeconds) Global Native
Int Function GetMenuAction() Global Native
Int Function GetMenuActionSlot() Global Native
String Function GetMenuActionName() Global Native
Function ClearMenuAction() Global Native

Actor Function GetCameraTarget() Global Native
String Function GetActorName(Actor akTarget) Global Native
Bool Function IsValidTarget(Actor akTarget) Global Native
Bool Function IsOutfitItem(Form akItem) Global Native
Bool Function IsPowerArmorItem(Form akItem) Global Native
Bool Function IsPowerArmorReference(ObjectReference akRef) Global Native
Bool Function AreOutfitNotificationsEnabled() Global Native

Int Function GetCurrentSlot() Global Native
Function SetCurrentSlot(Int aiSlot) Global Native
Bool Function SetSlotName(Int aiSlot, String asName) Global Native
Int Function GetLastRandomSlot() Global Native
Int Function GetLastEquippedSlot() Global Native
Function SetLastEquippedSlot(Int aiSlot) Global Native
Bool Function IsEquipBusy() Global Native
Float Function GetEquipCooldownRemaining(Float afNow) Global Native
Bool Function BeginEquipAction(Float afNow, Float afCooldownSeconds) Global Native
Function FinishEquipAction() Global Native
Bool Function IsRandomBusy() Global Native
Float Function GetRandomCooldownRemaining(Float afNow) Global Native
Bool Function BeginRandom(Float afNow, Float afCooldownSeconds) Global Native
Function FinishRandom(Int aiSlot) Global Native
Int Function ChooseRandomSlot(Int aiTargetSex, Int aiCurrentSlot, Int aiLastRandomSlot) Global Native
Int Function FindMatchingSlot(Int aiCurrentSlot, Int aiDirection, Int aiTargetSex) Global Native

Bool Function HasSlotData(Int aiSlot) Global Native
Int Function GetSlotGender(Int aiSlot) Global Native

Int Function SaveOutfit(Int aiSlot, Actor akTarget) Global Native
Int Function SavePowerArmor(Int aiSlot, ObjectReference akFrame) Global Native
ObjectReference Function GetSavedPowerArmorRef(Int aiSlot) Global Native
ObjectReference Function GetPrimaryPowerArmorRef() Global Native
Int Function RecallPrimaryPowerArmor() Global Native
Bool Function ClearPrimaryPowerArmorBinding() Global Native
ObjectReference Function GetLookedAtPowerArmor() Global Native
Int Function DeployPowerArmor(Int aiSlot, Actor akPlayer) Global Native
Bool Function PlayDeployEffect(ObjectReference akFrame) Global Native
Bool Function PlayPrimaryRecallEffect() Global Native
Int Function FindPowerArmorSlot(ObjectReference akFrame) Global Native
Bool Function BeginSaveOutfit(Int aiSlot, Int aiGender) Global Native
Bool Function AddSaveOutfitItem(Int aiSlot, Form akItem) Global Native
Int Function CommitSaveOutfit(Int aiSlot) Global Native
Int Function LoadOutfit(Int aiSlot, Actor akTarget) Global Native
Form Function GetSavedOutfitItem(Int aiSlot, Int aiIndex) Global Native
Int Function GetSavedOutfitItemID(Int aiSlot, Int aiIndex) Global Native
String Function GetSavedOutfitDebug(Int aiSlot) Global Native
String Function GetSavedOutfitSummary(Int aiSlot) Global Native
Bool Function ClearSlotData(Int aiSlot) Global Native
Bool Function EnsureDefaultOutfitRecorded(Actor akTarget) Global Native
Bool Function RecordManagedItem(Actor akTarget, Form akItem) Global Native
Int Function GetManagedItemCount(Actor akTarget) Global Native
Form Function GetManagedItem(Actor akTarget, Int aiIndex) Global Native
Int Function GetManagedItemID(Actor akTarget, Int aiIndex) Global Native
Function ClearManagedItems(Actor akTarget) Global Native
Int Function GetDefaultOutfitCount(Actor akTarget) Global Native
Form Function GetDefaultOutfitItem(Actor akTarget, Int aiIndex) Global Native
Int Function GetDefaultOutfitItemID(Actor akTarget, Int aiIndex) Global Native

String Function DebugGetInventoryInfo(Actor akTarget) Global Native
Function RefreshActorModel(Actor akTarget) Global Native
