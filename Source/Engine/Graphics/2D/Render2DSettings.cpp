#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettings.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "Render2DSettings" );

    namespace
    {
        struct Render2DSettingsInternal
        {
            static constexpr const utf8* kDefaultLayerName = "Default";

            /** @brief 정렬 방식 이름입니다. 모르는 이름은 읽기 오류입니다. */
            struct SortModeName
            {
                const utf8*          _pName;
                TransparencySortMode _mode;
            };
            static constexpr SortModeName kArrSortModeName[] = {
                {      "Auto",       TransparencySortMode::Auto},
                {  "Distance",   TransparencySortMode::Distance},
                {  "ViewAxis",   TransparencySortMode::ViewAxis},
                {"CustomAxis", TransparencySortMode::CustomAxis},
            };

            /** @brief "x y z" 를 읽습니다. 숫자 셋이 아니면(넷째 토큰이 숫자든 아니든) false 입니다. */
            [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue )
            {
                float32 arrValue[3]{};
                if ( StringUtil::parseExactFloats( text, " ,\t", arrValue, 3 ) == false )
                    return false;
                outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }
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
            SW_LOG_ERROR( "2D render settings '%#' not found", path );
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool Render2DSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        using Internal = Render2DSettingsInternal;
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "%#", doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( "Render2DSettings" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element must be <Render2DSettings>", sourceName );
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
                SW_LOG_ERROR( "%#: unknown element <%#> (TransparencySort, SortingLayers)", sourceName, child.getName() );
            }
            if ( bRead == false )
                return false;
        }

        if ( bHasLayers == false )
        {
            SW_LOG_ERROR( "%#: <SortingLayers> is missing (it must list at least 'Default')", sourceName );
            return false;
        }
        if ( loaded._listSortingLayer.size() > kMaxSortingLayerCount )
        {
            SW_LOG_ERROR( "%#: %# sorting layers exceed the limit %#", sourceName, loaded._listSortingLayer.size(), kMaxSortingLayerCount );
            return false;
        }
        const int32 defaultIndex = loaded.findSortingLayer( hashed_string( Internal::kDefaultLayerName ) );
        if ( defaultIndex < 0 )
        {
            SW_LOG_ERROR( "%#: the sorting layer list must contain 'Default' (renderers without a layer use it)", sourceName );
            return false;
        }
        loaded._defaultLayerIndex = static_cast<uint32>( defaultIndex );
        *this                     = loaded;
        return true;
    }

    bool Render2DSettings::readTransparencySort( const XMLNode& node, string_view sourceName )
    {
        using Internal                           = Render2DSettingsInternal;
        static constexpr const utf8* kArrKnown[] = { "mode", "axis" };
        if ( XMLNameCheck::reportUnknownAttributes( node, kArrKnown, sourceName ) == false )
            return false;
        const string_view modeName = node.getAttributeText( "mode" );
        bool              bFound   = modeName.empty();
        for ( const Internal::SortModeName& entry : Internal::kArrSortModeName )
        {
            if ( StringUtil::equals( modeName, entry._pName ) )
            {
                _sortMode = entry._mode;
                bFound    = true;
            }
        }
        if ( bFound == false )
        {
            SW_LOG_ERROR( "%#: unknown transparency sort mode '%#' (Auto, Distance, ViewAxis, CustomAxis)", sourceName, modeName );
            return false;
        }
        const string_view axisText = node.getAttributeText( "axis" );
        if ( axisText.empty() )
            return true;
        float3 axis{};
        if ( Internal::parseFloat3( axisText, axis ) == false || axis.getLengthSquared() <= MathUtil::kEpsilon )
        {
            SW_LOG_ERROR( "%#: transparency sort axis '%#' must be three numbers with a non-zero length", sourceName, axisText );
            return false;
        }
        axis.normalize();
        _sortAxis = axis;
        return true;
    }

    bool Render2DSettings::readSortingLayers( const XMLNode& node, string_view sourceName )
    {
        static constexpr const utf8* kArrKnown[] = { "name" };
        for ( XMLNode layerNode = node.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
        {
            if ( StringUtil::equals( layerNode.getName(), "Layer" ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> in <SortingLayers> (only <Layer name=\"...\"/>)", sourceName, layerNode.getName() );
                return false;
            }
            if ( XMLNameCheck::reportUnknownAttributes( layerNode, kArrKnown, sourceName ) == false )
                return false;
            const string_view name = layerNode.getAttributeText( "name" );
            if ( name.empty() )
            {
                SW_LOG_ERROR( "%#: a sorting layer has no name", sourceName );
                return false;
            }
            const hashed_string layerName( name );
            if ( findSortingLayer( layerName ) >= 0 )
            {
                SW_LOG_ERROR( "%#: sorting layer '%#' is listed twice", sourceName, name );
                return false;
            }
            _listSortingLayer.push_back( layerName );
        }
        return true;
    }

    int32 Render2DSettings::findSortingLayer( const hashed_string& layerName ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listSortingLayer.size(); ++layerIndex )
        {
            if ( _listSortingLayer[layerIndex] == layerName )
                return static_cast<int32>( layerIndex );
        }
        return -1;
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
        const int32 layerIndex = findSortingLayer( layerName );
        if ( layerIndex < 0 )
        {
            outSortKey = makeSortKey( _defaultLayerIndex, orderInLayer );
            return false;
        }
        outSortKey = makeSortKey( static_cast<uint32>( layerIndex ), orderInLayer );
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
    SW_RESOURCE_DATA_KIND( Render2D, "render2d", nullptr, nullptr, "/data/render2d.xml", &AssetDataKindUtil::loadCatalog<Render2DSettings> );
} // namespace sw
