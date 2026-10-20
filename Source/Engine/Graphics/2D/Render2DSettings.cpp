#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettings.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/LifetimeScope.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
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

            /** @brief "x y z" 를 읽습니다. 숫자 셋이 아니면(넷째 숫자가 있어도) false 입니다. */
            [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue )
            {
                float32 arrValue[4]{};
                if ( StringUtil::parseFloatList( text, " ,\t", arrValue, 4 ) != 3 )
                    return false;
                outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }

            /**
             * @brief 활성 표의 저장소입니다. 읽기 · 다시 읽기만 잠그고, 읽힌 뒤의 조회는 잠그지 않습니다.
             * @details 엔진 기동 뒤 처음 읽혀 채워지고, 엔진 수명 범위가 닫히면(엔진 종료) 소멸합니다(`s_render2DActiveStorage`).
             */
            struct ActiveStorage
            {
                Render2DSettings _settings;
                mutex            _loadMutex;
                atomic<bool>     _bLoaded;
            };

            /** @brief 활성 표의 저장소입니다. 엔진 수명이 끝난 뒤면 단언하고, 읽지 않은 빈 표를 돌려줍니다. */
            static ActiveStorage& getStorage();

            /**
             * @brief 엔진 파일(정본 하나)을 읽어 @p outSettings 에 넣습니다. 못 읽으면 오류를 남기고 false 이며 표는 그대로입니다.
             * @details 첫 읽기가 리소스 루트를 잡기 전에 올 수 있다(엔진 기동 없이 도는 테스트) — 루트를 못 잡는 것도 실패다.
             */
            [[nodiscard]] static bool loadActive( Render2DSettings& outSettings )
            {
                if ( ResourcePaths::initialize() == false )
                {
                    SW_LOG_ERROR( "2D render settings cannot be read - resource paths are not initialized" );
                    return false;
                }
                Render2DSettings engineSettings;
                if ( engineSettings.loadFromResource( Render2DSettings::getEngineSettingsPath() ) == false )
                    return false;
                outSettings = engineSettings;
                return true;
            }
        };

        ScopeOwned<Render2DSettingsInternal::ActiveStorage> s_render2DActiveStorage;

        Render2DSettingsInternal::ActiveStorage& Render2DSettingsInternal::getStorage()
        {
            ActiveStorage* const pStorage = s_render2DActiveStorage.getOrCreate();
            if ( pStorage != nullptr )
                return *pStorage;
            // 엔진 종료 뒤의 접근이다(단언은 `getOrCreate` 가 했다). 파일을 읽지 않는 기본 표로 답한다.
            static ActiveStorage s_closedFallback;
            return s_closedFallback;
        }
    } // namespace

    Render2DSettings::Render2DSettings()
        : _listSortingLayer{}
        , _sortAxis{ 0.0f, 0.0f, 1.0f }
        , _defaultLayerIndex{ 0 }
        , _sortMode{ TransparencySortMode::Auto }
    {
    }

    bool Render2DSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
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
            if ( StringUtil::equals( child.getName(), "TransparencySort", true ) )
            {
                static constexpr const utf8* kArrKnown[] = { "mode", "axis" };
                if ( XMLNameCheck::reportUnknownAttributes( child, kArrKnown, sourceName ) == false )
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
                for ( XMLNode layerNode = child.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
                {
                    static constexpr const utf8* kArrKnown[] = { "name" };
                    if ( StringUtil::equals( layerNode.getName(), "Layer", true ) == false )
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
        Render2DSettingsInternal::ActiveStorage& storage = Render2DSettingsInternal::getStorage();
        if ( storage._bLoaded.load( std::memory_order_acquire ) == false )
        {
            std::lock_guard<mutex> lock( storage._loadMutex );
            if ( storage._bLoaded.load( std::memory_order_relaxed ) == false )
            {
                // 렌더러 초기화(`reloadActive`)가 먼저 읽는다. 여기로 오는 것은 렌더러 없이 표를 보는 도구 · 테스트다.
                // 못 읽으면 단언하고 빈 표(레이어 없음 — 이름 있는 레이어는 모두 "모르는 레이어" 오류)로 답한다. 기본 표를 지어내지 않는다.
                const bool bLoaded = Render2DSettingsInternal::loadActive( storage._settings );
                SW_LOG_ASSERT( bLoaded, "2D render settings '%#' could not be loaded", getEngineSettingsPath() );
                storage._bLoaded.store( true, std::memory_order_release );
            }
        }
        return storage._settings;
    }

    bool Render2DSettings::reloadActive()
    {
        Render2DSettingsInternal::ActiveStorage& storage = Render2DSettingsInternal::getStorage();
        std::lock_guard<mutex>                   lock( storage._loadMutex );
        if ( Render2DSettingsInternal::loadActive( storage._settings ) == false )
            return false;
        storage._bLoaded.store( true, std::memory_order_release );
        return true;
    }
} // namespace sw
