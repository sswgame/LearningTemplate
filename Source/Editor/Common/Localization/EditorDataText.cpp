#include "pch.h"

#include "Editor/Common/Localization/EditorDataText.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Localization/LocalizationManager.h"

namespace sw::editor
{
    string EditorDataText::localize( string_view source )
    {
        if ( source.empty() )
            return string{};
        const LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>();
        const utf8* const                pFound        = pLocalization != nullptr ? pLocalization->findStringByText( source ) : nullptr;
        return pFound != nullptr ? string( pFound ) : string( source );
    }

    uint32 EditorDataText::getRevision()
    {
        const LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>();
        return pLocalization != nullptr ? pLocalization->getTextRevision() : 0u;
    }
} // namespace sw::editor
