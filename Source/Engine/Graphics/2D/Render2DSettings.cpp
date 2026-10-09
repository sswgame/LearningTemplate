#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettings.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Xml/XmlDocument.h"
#include "Engine/Serialization/Xml/XmlNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "Render2DSettings" );

    namespace
    {
        struct Render2DSettingsInternal
        {
            static constexpr const utf8* kDefaultLayerName = "Default";

            /** @brief 정렬 방식 이름표입니다. 모르는 이름은 읽기 오류입니다. */
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

            /** @brief "x y z" 를 읽습니다. 숫자 셋이 아니면 false 입니다. */
            [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue )
            {
                float32 arrValue[3]{};
                uint32  count  = 0;
                size_t  cursor = 0;
                while ( cursor < text.size() )
                {
                    while ( cursor < text.size() && ( text[cursor] == ' ' || text[cursor] == ',' || text[cursor] == '\t' ) )
                    {
                        ++cursor;
                    }
                    const size_t start = cursor;
                    while ( cursor < text.size() && text[cursor] != ' ' && text[cursor] != ',' && text[cursor] != '\t' )
                    {
                        ++cursor;
                    }
                    if ( cursor == start )
                        break;
                    if ( count >= 3 || StringUtil::parseFloat( text.substr( start, cursor - start ), arrValue[count] ) == false )
                        return false;
                    ++count;
                }
                if ( count != 3 )
                    return false;
                outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }

            /** @brief 활성 표의 저장소입니다. 처음 읽기는 잠그고, 그 뒤로는 읽기만 합니다. */
            static Render2DSettings& getStorage()
            {
                static Render2DSettings s_settings;
                return s_settings;
            }

            /** @brief 활성 표 저장소를 파일에서 채웁니다. */
            static void loadStorage() { loadActive( getStorage() ); }

            /** @brief 활성 게임 팩의 파일, 없으면 엔진 파일을 읽어 @p outSettings 에 넣습니다. */
            static void loadActive( Render2DSettings& outSettings )
            {
                // 첫 읽기가 리소스 루트를 잡기 전에 올 수 있다(엔진 기동 없이 도는 시험). 그대로 읽으면 기본 표가 프로세스 끝까지 남는다.
                (void)ResourceUtil::initialize();
                const string& packRoot = GameConfig::getActive()._packRoot;
                if ( packRoot.empty() == false )
                {
                    const string gamePath = FileUtil::joinPath( packRoot, "data/render2d.xml" );
                    if ( ResourceUtil::hasResource( gamePath ) )
                    {
                        Render2DSettings gameSettings;
                        if ( gameSettings.loadFromResource( gamePath ) )
                        {
                            outSettings = gameSettings;
                            return;
                        }
                    }
                }
                Render2DSettings engineSettings;
                if ( engineSettings.loadFromResource( Render2DSettings::getEngineSettingsPath() ) )
                    outSettings = engineSettings;
                else
                    outSettings = Render2DSettings{};
            }
        };
    } // namespace

    Render2DSettings::Render2DSettings()
        : _listSortingLayer{ hashed_string( Render2DSettingsInternal::kDefaultLayerName ) }
        , _sortAxis{ 0.0f, 0.0f, 1.0f }
        , _defaultLayerIndex{ 0 }
        , _sortMode{ TransparencySortMode::Auto }
    {
    }

    bool Render2DSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "2D render settings '%#' not found", path );
            return false;
        }
        return loadFromXmlText( text, path );
    }

    bool Render2DSettings::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        using Internal = Render2DSettingsInternal;
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "%#", doc.getLastError() );
            return false;
        }
        const XmlNode root = doc.getRoot( "Render2DSettings" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element must be <Render2DSettings>", sourceName );
            return false;
        }

        Render2DSettings loaded;
        loaded._listSortingLayer.clear();
        bool bHasLayers = false;
        for ( XmlNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "TransparencySort", true ) )
            {
                static constexpr const utf8* kArrKnown[] = { "mode", "axis" };
                if ( XmlNameCheck::reportUnknownAttributes( child, kArrKnown, sourceName ) == false )
                    return false;
                const string_view modeName = child.getAttributeText( "mode" );
                bool              bFound   = modeName.empty();
                for ( const Internal::SortModeName& entry : Internal::kArrSortModeName )
                {
                    if ( StringUtil::equals( modeName, entry._pName, true ) )
                    {
                        loaded._sortMode = entry._mode;
                        bFound           = true;
                    }
                }
                if ( bFound == false )
                {
                    SW_LOG_ERROR( "%#: unknown transparency sort mode '%#' (Auto, Distance, ViewAxis, CustomAxis)", sourceName, modeName );
                    return false;
                }
                const string_view axisText = child.getAttributeText( "axis" );
                if ( axisText.empty() == false )
                {
                    float3 axis{};
                    if ( Internal::parseFloat3( axisText, axis ) == false || axis.getLengthSquared() <= MathUtil::kEpsilon )
                    {
                        SW_LOG_ERROR( "%#: transparency sort axis '%#' must be three numbers with a non-zero length", sourceName, axisText );
                        return false;
                    }
                    axis.normalize();
                    loaded._sortAxis = axis;
                }
            }
            else if ( StringUtil::equals( child.getName(), "SortingLayers", true ) )
            {
                bHasLayers = true;
                for ( XmlNode layerNode = child.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
                {
                    static constexpr const utf8* kArrKnown[] = { "name" };
                    if ( StringUtil::equals( layerNode.getName(), "Layer", true ) == false )
                    {
                        SW_LOG_ERROR( "%#: unknown element <%#> in <SortingLayers> (only <Layer name=\"...\"/>)", sourceName, layerNode.getName() );
                        return false;
                    }
                    if ( XmlNameCheck::reportUnknownAttributes( layerNode, kArrKnown, sourceName ) == false )
                        return false;
                    const string_view name = layerNode.getAttributeText( "name" );
                    if ( name.empty() )
                    {
                        SW_LOG_ERROR( "%#: a sorting layer has no name", sourceName );
                        return false;
                    }
                    const hashed_string layerName( name );
                    if ( loaded.findSortingLayer( layerName ) >= 0 )
                    {
                        SW_LOG_ERROR( "%#: sorting layer '%#' is listed twice", sourceName, name );
                        return false;
                    }
                    loaded._listSortingLayer.push_back( layerName );
                }
            }
            else
            {
                SW_LOG_ERROR( "%#: unknown element <%#> (TransparencySort, SortingLayers)", sourceName, child.getName() );
                return false;
            }
        }

        if ( bHasLayers == false )
            loaded._listSortingLayer.push_back( hashed_string( Internal::kDefaultLayerName ) );
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

    const hashed_string& Render2DSettings::getSortingLayerName( uint32 layerIndex ) const
    {
        static const hashed_string s_empty{};
        return ( layerIndex < _listSortingLayer.size() ) ? _listSortingLayer[layerIndex] : s_empty;
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

    const Render2DSettings& Render2DSettings::getActive()
    {
        static std::once_flag s_onceLoad;
        std::call_once( s_onceLoad, &Render2DSettingsInternal::loadStorage );
        return Render2DSettingsInternal::getStorage();
    }

    void Render2DSettings::reloadActive()
    {
        (void)getActive(); // 첫 읽기가 뒤에 다시 덮어쓰지 않게 먼저 끝낸다
        Render2DSettingsInternal::loadActive( Render2DSettingsInternal::getStorage() );
    }
} // namespace sw
