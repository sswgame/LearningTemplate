#include "pch.h"

#include "Engine/Localization/LocText.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw
{
    const utf8* LocalizedText::find( string_view fullKey, const utf8* pSourceText )
    {
        if ( engine::areEngineServicesBound() == false )
            return pSourceText;
        const LocalizationManager& localization = engine::getSubsystem<LocalizationManager>();
        const utf8*                pFound       = localization.findStringByText( fullKey );
        if ( pFound != nullptr )
            return pFound;
        localization.reportMissingKey( fullKey );
        return pSourceText;
    }

    string LocalizedText::format( string_view fullKey, const utf8* pSourceText, const TextArgumentList& arguments )
    {
        const utf8* pPattern = find( fullKey, pSourceText );
        if ( engine::areEngineServicesBound() == false )
        {
            string text;
            (void)TextFormatter::format( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments, CultureInfo{}, text );
            return text;
        }
        return engine::getSubsystem<LocalizationManager>().formatText( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments );
    }
} // namespace sw
