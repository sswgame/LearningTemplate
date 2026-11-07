#include "pch.h"

#include "Engine/Rendering/MeshLODSettings.h"

#include "Core/Log/Logger.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool MeshLODSettings::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        return loadComposed( xml, sourceLabel, ResourceContext::forType( *StaticType() ) );
    }

    bool MeshLODSettings::loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        *this = MeshLODSettings{};
        // 키는 대소문자까지 맞아야 하고 모르는 속성 · 범위 밖 숫자는 오류다(`readComposedXMLDocument` 가 알린다). 두 값 모두 필수다(0 은 읽지 못한 값).
        const bool bRead = ConfigManager::readComposedXMLDocument( this, *StaticType(), xml, sourceLabel, context );
        if ( bRead && _refineHysteresis > 0.0f && _skinnedBoundsScale >= 1.0f )
            return true;
        SW_LOG_ERROR_ID( "MeshLODSettings.Invalid", "Mesh LOD settings '%#' need _refineHysteresis > 0 and _skinnedBoundsScale >= 1", sourceLabel );
        *this = MeshLODSettings{};
        return false;
    }

    bool MeshLODSettings::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "MeshLODSettings.CouldNotRead", "Mesh LOD settings '%#' could not be read", assetID.view() );
            *this = MeshLODSettings{};
            return false;
        }
        return loadComposed( text, assetID.view(), ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    const MeshLODSettings& MeshLODSettings::getDefault()
    {
        static const MeshLODSettings s_settings = []()
        {
            MeshLODSettings settings;
            const bool      bLoaded = settings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._meshLODSettings.get() } );
            SW_LOG_ASSERT( bLoaded, "Mesh LOD settings '%#' could not be loaded", engine::getSubsystem<EngineDefaultAssets>()._meshLODSettings.get() );
            return settings;
        }();
        return s_settings;
    }

    /** @brief 메시 LOD 정책 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( MeshLOD, AssetTypeDef::makeDataFile( "meshlod", AssetPathRule{ {}, {}, "/render/meshlod.xml" }, AssetTypeLoader::of<MeshLODSettings>() ) );
} // namespace sw
