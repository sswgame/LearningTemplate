#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettings.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        struct Render2DSettingsInternal
        {
            static constexpr const utf8* kDefaultLayerName = "Default";
        };
    } // namespace

    Render2DSettings::Render2DSettings()
        : _listSortingLayer{}
        , _sortAxis{ 0.0f, 0.0f, 1.0f }
        , _defaultLayerIndex{ 0 }
        , _sortMode{ TransparencySortMode::Auto }
    {
    }

    bool Render2DSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.DRenderSettingsNot", "2D render settings '%#' not found", path );
            return false;
        }
        // 리소스 id 로 합친다 — 조건 · 덮어쓰기 층 · 접속 대조를 받는다(글 입구 `loadFromXMLText` 는 id 가 없다).
        XMLDocument doc;
        if ( doc.parseResourceText( text, assetID ) == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.ParseFailed", "%#", doc.getLastError() );
            return false;
        }
        return loadFromDocument( doc, path );
    }

    bool Render2DSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        // 데이터 문서 길로 읽는다 — 조건(`_when`)을 푼다. 예전의 `parse` 는 조건을 풀지 않았다.
        XMLDocument doc;
        if ( doc.parseData( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.ParseFailed", "%#", doc.getLastError() );
            return false;
        }
        return loadFromDocument( doc, sourceName );
    }

    bool Render2DSettings::loadFromDocument( const XMLDocument& doc, string_view sourceName )
    {
        using Internal     = Render2DSettingsInternal;
        const XMLNode root = doc.getRoot( "Render2DSettings" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.RootElementMustRender2DSettings", "%#: the root element must be <Render2DSettings>", sourceName );
            return false;
        }

        Render2DSettings loaded;
        loaded._listSortingLayer.clear();
        bool bHasLayers = false;
        for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            bool bRead = false;
            if ( StringUtil::equals( child.getName(), "TransparencySort" ) )
            {
                bRead = loaded.readTransparencySort( child, sourceName );
            }
            else if ( StringUtil::equals( child.getName(), "SortingLayers" ) )
            {
                bHasLayers = true;
                bRead      = loaded.readSortingLayers( child, sourceName );
            }
            else
            {
                SW_LOG_ERROR_ID( "Render2DSettings.UnknownElementTransparencySortSortingLayers", "%#: unknown element <%#> (TransparencySort, SortingLayers)", sourceName, child.getName() );
            }
            if ( bRead == false )
                return false;
        }

        if ( bHasLayers == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.SortingLayersMissingMustList", "%#: <SortingLayers> is missing (it must list at least 'Default')", sourceName );
            return false;
        }
        if ( loaded._listSortingLayer.size() > kMaxSortingLayerCount )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.SortingLayersExceedLimit", "%#: %# sorting layers exceed the limit %#", sourceName, loaded._listSortingLayer.size(), kMaxSortingLayerCount );
            return false;
        }
        const optional<uint32> defaultIndex = loaded.findSortingLayer( hashed_string( Internal::kDefaultLayerName ) );
        if ( defaultIndex.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.SortingLayerListMust", "%#: the sorting layer list must contain 'Default' (renderers without a layer use it)", sourceName );
            return false;
        }
        loaded._defaultLayerIndex = *defaultIndex;
        *this                     = loaded;
        return true;
    }

    bool Render2DSettings::readTransparencySort( const XMLNode& node, string_view sourceName )
    {
        Render2DTransparencySortParams params;
        if ( XMLFields::readProperties( node, params, sourceName ) == false || XMLNameCheck::reportUnknownChildren( node, nullptr, 0, sourceName ) == false )
            return false;
        if ( params._axis.getLengthSquared() <= MathUtil::kEpsilon )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.TransparencySortAxisMust", "%#: transparency sort axis must have a non-zero length", sourceName );
            return false;
        }
        _sortMode = params._mode;
        _sortAxis = params._axis;
        _sortAxis.normalize();
        return true;
    }

    bool Render2DSettings::readSortingLayers( const XMLNode& node, string_view sourceName )
    {
        for ( XMLNode layerNode = node.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
        {
            if ( StringUtil::equals( layerNode.getName(), "Layer" ) == false )
            {
                SW_LOG_ERROR_ID( "Render2DSettings.UnknownElementSortingLayersOnly", "%#: unknown element <%#> in <SortingLayers> (only <Layer name=\"...\"/>)", sourceName, layerNode.getName() );
                return false;
            }
            Render2DSortingLayerParams params;
            if ( XMLFields::readProperties( layerNode, params, sourceName ) == false || params._name.empty() )
                return false;
            const hashed_string& layerName = params._name;
            if ( findSortingLayer( layerName ).has_value() )
            {
                SW_LOG_ERROR_ID( "Render2DSettings.SortingLayerListedTwice", "%#: sorting layer '%#' is listed twice", sourceName, layerName.c_str() );
                return false;
            }
            _listSortingLayer.push_back( layerName );
        }
        return true;
    }

    optional<uint32> Render2DSettings::findSortingLayer( const hashed_string& layerName ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listSortingLayer.size(); ++layerIndex )
        {
            if ( _listSortingLayer[layerIndex] == layerName )
                return static_cast<uint32>( layerIndex );
        }
        return nullopt;
    }

    uint32 Render2DSettings::makeSortKey( uint32 layerIndex, int32 orderInLayer )
    {
        const int32  order  = MathUtil::clamp( orderInLayer, kMinOrderInLayer, kMaxOrderInLayer );
        const uint32 layer  = MathUtil::min( layerIndex, kMaxSortingLayerCount - 1u );
        const uint32 biased = static_cast<uint32>( order + 0x8000 );
        return ( layer << 16 ) | biased;
    }

    bool Render2DSettings::resolveSortKey( const hashed_string& layerName, int32 orderInLayer, uint32& outSortKey ) const
    {
        if ( layerName.empty() )
        {
            outSortKey = makeSortKey( _defaultLayerIndex, orderInLayer );
            return true;
        }
        const optional<uint32> layerIndex = findSortingLayer( layerName );
        if ( layerIndex.has_value() == false )
        {
            outSortKey = makeSortKey( _defaultLayerIndex, orderInLayer );
            return false;
        }
        outSortKey = makeSortKey( *layerIndex, orderInLayer );
        return true;
    }

    float3 Render2DSettings::computeTransparentSortAxis( bool bOrthographic, const float3& cameraForward ) const
    {
        switch ( _sortMode )
        {
            case TransparencySortMode::Auto:
                return bOrthographic ? cameraForward : float3{ 0.0f, 0.0f, 0.0f };
            case TransparencySortMode::Distance:
                return float3{ 0.0f, 0.0f, 0.0f };
            case TransparencySortMode::ViewAxis:
                return cameraForward;
            case TransparencySortMode::CustomAxis:
                return _sortAxis;
        }
        return float3{ 0.0f, 0.0f, 0.0f };
    }

    /** @brief 2D 렌더 설정 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( Render2D, AssetTypeDef::makeDataFile( "render2d", AssetPathRule{ {}, {}, "/data/render2d.xml" }, AssetTypeLoader::of<Render2DSettings>() ) );
} // namespace sw

// 엔진 2D 설정은 상수로 읽는다 - 쓰는 것만 담는 팩 쿠킹의 뿌리다(`AssetCookRoot.h`).
SW_ASSET_COOK_ROOT( Render2DSettings, ::sw::Render2DSettings::getEngineSettingsPath() );
