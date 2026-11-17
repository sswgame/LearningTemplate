#include "pch.h"

#include "Engine/Config/RenderQualityOverride.h"

#include "Core/Log/Logger.h"

#include "Engine/Config/PlatformConfigOverride.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    bool RenderQualityOverride::makeOverrideJSON( string_view tiersJSON, string_view tierName, const utf8* pLabel, string& outOverrideJSON )
    {
        JSONDocument document;
        if ( document.parse( tiersJSON, pLabel ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_ERROR_ID( "Config.RenderQualityNotObject", "Render quality %#: not a JSON object of tier name -> _render override", pLabel );
            return false;
        }
        const JSONValue tier = document.getRoot().get( tierName );
        if ( tier.isValid() == false )
        {
            string listName;
            for ( const string& name : document.getRoot().getMemberNames() )
            {
                if ( listName.empty() == false )
                    listName += ", ";
                listName += name;
            }
            SW_LOG_ERROR_ID( "Config.RenderQualityUnknownTier", "Render quality %#: no tier '%#' (tiers: %#)", pLabel, tierName, listName );
            return false;
        }
        if ( tier.isObject() == false )
        {
            SW_LOG_ERROR_ID( "Config.RenderQualityTierNotObject", "Render quality %#: tier '%#' is not a JSON object", pLabel, tierName );
            return false;
        }
        const expected<string> tierText = tier.dump();
        if ( tierText.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Config.RenderQualityNotWritable", "Render quality %#: tier '%#' could not be written: %# (%#)", pLabel, tierName, tierText.error().message(),
                             tierText.error().detail() );
            return false;
        }
        outOverrideJSON = "{\"_render\":";
        outOverrideJSON += *tierText;
        outOverrideJSON += "}";
        return true;
    }

    bool RenderQualityOverride::apply( string_view baseJSON, string_view tiersJSON, string_view tierName, const utf8* pLabel, string& outJSON )
    {
        string overrideJSON;
        return makeOverrideJSON( tiersJSON, tierName, pLabel, overrideJSON ) && PlatformConfigOverride::apply( baseJSON, overrideJSON, pLabel, outJSON );
    }
} // namespace sw
