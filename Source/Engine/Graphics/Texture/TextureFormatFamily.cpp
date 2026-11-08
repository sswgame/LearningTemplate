#include "pch.h"

#include "Engine/Graphics/Texture/TextureFormatFamily.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct TextureFormatSelectionInternal
        {
            static constexpr string_view kDDSExtension = ".dds";

            /** @brief @p settings 의 묶음 하나를 형식 이름까지 풉니다. 모르는 이름이면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool resolveFamily( const TextureFormatFamilySettings& settings, const TypeRegistry& types, TextureFormatFamily& outFamily )
            {
                const auto resolveFormat = [&]( const string& name, RHIFormat& outFormat )
                {
                    if ( types.enumFromString( name, outFormat ) )
                        return true;
                    SW_LOG_ERROR_ID( "TextureFormats.UnknownFormat", "Texture format family '%#' names format '%#' which RHIFormat does not have", settings._name, name );
                    return false;
                };
                outFamily._name   = settings._name;
                outFamily._suffix = settings._suffix;
                return resolveFormat( settings._color, outFamily._color ) && resolveFormat( settings._normal, outFamily._normal ) && resolveFormat( settings._mask, outFamily._mask ) &&
                       resolveFormat( settings._hdr, outFamily._hdr );
            }

            /** @brief 형식 변형 @p suffix 의 곁 파일 경로입니다. 변형 자신의 곁 파일은 없습니다(`a.astc.dds` 에서 다시 따라가지 않는다). */
            static string makeCompanion( string_view assetID, string_view suffix )
            {
                const string_view stem     = assetID.substr( 0, assetID.size() - MathUtil::min( assetID.size(), kDDSExtension.size() ) );
                const bool        bVariant = StringUtil::endsWith( stem, ".astc" ) || StringUtil::endsWith( stem, ".etc2" );
                return bVariant ? string{} : TextureFormatFamily::makeVariantPath( assetID, suffix );
            }
            static string makeASTCCompanion( string_view assetID ) { return makeCompanion( assetID, ".astc" ); }
            static string makeETC2Companion( string_view assetID ) { return makeCompanion( assetID, ".etc2" ); }
        };
    } // namespace

    RHIFormat TextureFormatFamily::getFormat( TextureFormatUsage usage ) const
    {
        switch ( usage )
        {
            case TextureFormatUsage::Color:
                return _color;
            case TextureFormatUsage::Normal:
                return _normal;
            case TextureFormatUsage::Mask:
                return _mask;
            case TextureFormatUsage::HDR:
                return _hdr;
            case TextureFormatUsage::Raw:
                return RHIFormat::Unknown;
        }
        return RHIFormat::Unknown;
    }

    bool TextureFormatFamily::resolveTarget( const TextureFormatSettings& settings, string_view targetName, const TypeRegistry& types, vector<TextureFormatFamily>& outListFamily )
    {
        outListFamily.clear();
        const TextureFormatTargetSettings* pTarget = nullptr;
        for ( const TextureFormatTargetSettings& target : settings._listTarget )
        {
            if ( target._name == targetName )
                pTarget = &target;
        }
        if ( pTarget == nullptr || pTarget->_listFamily.empty() )
        {
            SW_LOG_ERROR_ID( "TextureFormats.UnknownTarget", "Texture format target '%#' is not in the table or lists no family", targetName );
            return false;
        }
        for ( const string& familyName : pTarget->_listFamily )
        {
            const TextureFormatFamilySettings* pFamily = nullptr;
            for ( const TextureFormatFamilySettings& family : settings._listFamily )
            {
                if ( family._name == familyName )
                    pFamily = &family;
            }
            TextureFormatFamily resolved;
            if ( pFamily == nullptr )
                SW_LOG_ERROR_ID( "TextureFormats.UnknownFamily", "Texture format target '%#' lists family '%#' which the table does not have", targetName, familyName );
            if ( pFamily == nullptr || TextureFormatSelectionInternal::resolveFamily( *pFamily, types, resolved ) == false )
            {
                outListFamily.clear();
                return false;
            }
            outListFamily.push_back( std::move( resolved ) );
        }
        return true;
    }

    string TextureFormatFamily::findPlatformTarget( const TextureFormatSettings& settings, string_view platformName )
    {
        string targetName;
        uint32 matchCount{ 0 };
        for ( const TextureFormatTargetSettings& target : settings._listTarget )
        {
            for ( const string& platform : target._listPlatform )
            {
                if ( platform != platformName )
                    continue;
                targetName = target._name;
                ++matchCount;
            }
        }
        if ( matchCount != 1 )
        {
            SW_LOG_ERROR_ID( "TextureFormats.PlatformTarget", "Platform '%#' is listed by %# texture format targets - exactly one target must list it (_listPlatform)", platformName, matchCount );
            return {};
        }
        return targetName;
    }

    bool TextureFormatFamily::resolveCookFamilies( const TextureFormatSettings& settings, const TypeRegistry& types, vector<TextureFormatFamily>& outListFamily )
    {
        outListFamily.clear();
        for ( const TextureFormatTargetSettings& target : settings._listTarget )
        {
            vector<TextureFormatFamily> listTargetFamily;
            if ( resolveTarget( settings, target._name, types, listTargetFamily ) == false )
            {
                outListFamily.clear();
                return false; // 까닭은 resolveTarget 이 남겼다
            }
            for ( TextureFormatFamily& family : listTargetFamily )
            {
                bool bListed{ false };
                for ( const TextureFormatFamily& listed : outListFamily )
                {
                    bListed = bListed || listed._name == family._name;
                }
                if ( family._suffix.empty() == false && bListed == false )
                    outListFamily.push_back( std::move( family ) );
            }
        }
        return true;
    }

    const TextureFormatFamily* TextureFormatFamily::selectFamily( const vector<TextureFormatFamily>& listFamily, const IRHIDevice& device )
    {
        for ( const TextureFormatFamily& family : listFamily )
        {
            const bool bSupported = device.supportsTextureFormat( family._color ) && device.supportsTextureFormat( family._normal ) && device.supportsTextureFormat( family._mask ) &&
                                    device.supportsTextureFormat( family._hdr );
            if ( bSupported )
                return &family;
        }
        return nullptr;
    }

    string TextureFormatFamily::selectVariantSuffix( const IRHIDevice& device )
    {
        if ( engine::areEngineServicesBound() == false )
            return {};
        const EngineConfig* const pConfig = engine::findSubsystem<EngineConfig>();
        const TypeRegistry* const pTypes  = engine::findSubsystem<TypeRegistry>();
        if ( pConfig == nullptr || pTypes == nullptr )
            return {};
        const TextureFormatSettings& settings   = pConfig->_textureFormats;
        const string                 targetName = findPlatformTarget( settings, build::kPlatformName );
        vector<TextureFormatFamily>  listFamily;
        if ( targetName.empty() || resolveTarget( settings, targetName, *pTypes, listFamily ) == false )
            return {}; // 까닭은 위에서 남겼다
        const TextureFormatFamily* const pFamily = selectFamily( listFamily, device );
        if ( pFamily == nullptr )
        {
            SW_LOG_ERROR_ID( "TextureFormats.NoSupportedFamily", "This device supports no texture format family of target '%#'", targetName );
            return {};
        }
        SW_LOG_INFO_ID( "TextureFormats.FamilySelected", "Texture format family '%#' selected for target '%#'", pFamily->_name, targetName );
        return pFamily->_suffix;
    }

    string TextureFormatFamily::makeVariantPath( string_view ddsPath, string_view suffix )
    {
        constexpr string_view kExtension = TextureFormatSelectionInternal::kDDSExtension;
        if ( StringUtil::endsWith( ddsPath, kExtension ) == false )
            return {};
        return string( ddsPath.substr( 0, ddsPath.size() - kExtension.size() ) ) + string( suffix ) + string( kExtension );
    }
} // namespace sw

// 쿠킹 변형은 칸이 아니라 이름 규칙으로 찾는다 — 원본 DDS 가 쓰이면 곁에 있는 변형을 함께 담는다. 접미사는 표(`EngineConfig.json`)와 같아야 한다(TextureFormatSelectionTest.CompanionsCoverEveryFamilySuffix).
SW_ASSET_COMPANION( TextureFormatASTC, &::sw::TextureFormatSelectionInternal::makeASTCCompanion );
SW_ASSET_COMPANION( TextureFormatETC2, &::sw::TextureFormatSelectionInternal::makeETC2Companion );
