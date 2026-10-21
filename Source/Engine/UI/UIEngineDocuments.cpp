#include "pch.h"

#include "Engine/UI/UIEngineDocuments.h"

#include "Engine/Config/EngineDefaultAssets.h"

namespace sw
{
    UIEngineDocuments UIEngineDocuments::makeFromDefaultAssets( const EngineDefaultAssets& assets )
    {
        UIEngineDocuments documents;
        documents._optionsMenu          = assets._uiOptionsMenu.get();
        documents._optionsRowBool       = assets._uiOptionsRowBool.get();
        documents._optionsRowInt        = assets._uiOptionsRowInt.get();
        documents._optionsRowFloat      = assets._uiOptionsRowFloat.get();
        documents._optionsRowEnum       = assets._uiOptionsRowEnum.get();
        documents._optionsRowEnumPad    = assets._uiOptionsRowEnumPad.get();
        documents._optionsRowKeyBinding = assets._uiOptionsRowKeyBinding.get();
        documents._optionsRowString     = assets._uiOptionsRowString.get();
        documents._settingsConfirm      = assets._uiSettingsConfirm.get();
        documents._settingsUnsaved      = assets._uiSettingsUnsaved.get();
        documents._keyRebind            = assets._uiKeyRebind.get();
        documents._notifications        = assets._uiNotifications.get();
        documents._notificationEntry    = assets._uiNotificationEntry.get();
        documents._subtitles            = assets._uiSubtitles.get();
        documents._tooltip              = assets._uiTooltip.get();
        documents._debugHUD             = assets._uiDebugHUD.get();
        return documents;
    }
} // namespace sw
