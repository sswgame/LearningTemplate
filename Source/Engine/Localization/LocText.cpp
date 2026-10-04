#include "pch.h"

#include "Engine/Localization/LocText.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw
{
    const utf8* LocText::find( string_view fullKey, const utf8* pSourceText )
    {
        if ( engine::areEngineServicesBound() == false )
            return pSourceText;
        const LocalizationManager& localization = engine::getLocalizationManager();
        const utf8*                pFound       = localization.getStringByText( fullKey, nullptr );
        if ( pFound != nullptr )
            return pFound;
        localization.reportMissingKey( fullKey );
        return pSourceText;
    }

    string LocText::format( string_view fullKey, const utf8* pSourceText, const TextArgumentList& arguments )
    {
        const utf8* pPattern = find( fullKey, pSourceText );
        if ( engine::areEngineServicesBound() == false )
        {
            string text;
            (void)TextFormatter::format( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments, CultureInfo{}, text );
            return text;
        }
        return engine::getLocalizationManager().formatText( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments );
    }
} // namespace sw
