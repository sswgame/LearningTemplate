#include "pch.h"

#include "Engine/Config/ConfigOverride.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    namespace
    {
        /** @brief @p source 의 키마다 @p target 에 덮습니다. 둘 다 객체인 칸만 안으로 들어간다. */
        void mergeMembers( const JSONValue& target, const JSONValue& source )
        {
            for ( const string& key : source.getMemberNames() )
            {
                const JSONValue sourceValue = source.get( key );
                const JSONValue targetValue = target.get( key );
                if ( sourceValue.isObject() && targetValue.isValid() && targetValue.isObject() )
                {
                    mergeMembers( targetValue, sourceValue );
                    continue;
                }
                target.set( key ).assignFrom( sourceValue );
            }
        }
    } // namespace

    string PlatformConfigOverride::makeEngineConfigPath( string_view platformName )
    {
        const string folder = FileUtil::joinPath( config::kDirRuntimePlatformConfig, platformName );
        return FileUtil::joinPath( folder, FileUtil::getFileNamePart( config::kFileRuntimeEngineConfig ) );
    }

    string PlatformConfigOverride::makeEngineConfigPath()
    {
        return makeEngineConfigPath( build::kPlatformName );
    }

    bool PlatformConfigOverride::apply( string_view baseJSON, string_view overrideJSON, const utf8* pOverrideLabel, string& outJSON )
    {
        JSONDocument baseDocument;
        JSONDocument overrideDocument;
        if ( baseDocument.parse( baseJSON ) == false || baseDocument.getRoot().isObject() == false )
        {
            SW_LOG_ERROR_ID( "Config.OverrideBaseNotObject", "Config override %#: the base layer is not a JSON object", pOverrideLabel );
            return false;
        }
        if ( overrideDocument.parse( overrideJSON, pOverrideLabel ) == false || overrideDocument.getRoot().isObject() == false )
        {
            SW_LOG_ERROR_ID( "Config.OverrideNotObject", "Config override %#: not a JSON object", pOverrideLabel );
            return false;
        }
        mergeMembers( baseDocument.getRoot(), overrideDocument.getRoot() );
        expected<string> merged = baseDocument.dump();
        if ( merged.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Config.OverrideNotWritable", "Config override %#: the merged config could not be written: %# (%#)", pOverrideLabel, merged.error().message(),
                             merged.error().detail() );
            return false;
        }
        outJSON = std::move( *merged );
        return true;
    }

    bool PlatformConfigOverride::composeBakedEngineConfig( string& outJSON )
    {
        if ( shipping_host::kEngineConfigPlatformOverrideJSON == nullptr )
        {
            outJSON = shipping_host::kEngineConfigJSON;
            return true;
        }
        StringBuilder<constant::buffer::kMax256> label;
        label.appendFormat( "%# (configure-time copy)", makeEngineConfigPath() );
        return apply( shipping_host::kEngineConfigJSON, shipping_host::kEngineConfigPlatformOverrideJSON, label.c_str(), outJSON );
    }
} // namespace sw

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
