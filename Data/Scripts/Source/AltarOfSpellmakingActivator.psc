Scriptname AltarOfSpellmakingActivator extends ObjectReference
{Attached to the physical Altar of Spellmaking workbench in the game world}

Event OnActivate(ObjectReference akActionRef)
    Actor player = Game.GetPlayer()
    If (akActionRef != player)
        Return
    EndIf

    Debug.Notification("Communing with the Altar of Spellmaking...")

    ; Form 0x0000000F corresponds to Skyrim's universal Gold001 currency FormID
    Form goldForm = Game.GetForm(0x0000000F)
    Int gold = 0
    If (goldForm)
        gold = player.GetItemCount(goldForm)
    EndIf

    ; Retrieve Magicka attributes safely via Papyrus engine primitives
    Int maxMagicka = player.GetBaseActorValue("Magicka") as Int
    Int currentMagicka = player.GetActorValue("Magicka") as Int

    ; Open interactive PrismaUI interface passing verified primitive parameters
    Bool opened = AltarOfSpellmaking.OpenAltarMenu(gold, maxMagicka, currentMagicka)

    If (!opened)
        Debug.Notification("The arcane energies failed to coalesce.")
    EndIf
EndEvent