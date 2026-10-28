#include "pch.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPreview.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetIO.h"
#include "Engine/Config/GameConfig.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ParkLayoutPreview" );

    namespace
    {
        struct ParkLayoutPreviewInternal
        {
            /** @brief 트랙 선분을 이을 때 건너뛰는 점 간격입니다. 트랙 점은 0.25 m 마다 있어서 그대로 그리면 선분이 수천 개가 됩니다. */
            static constexpr size_t kTrackPointStride = 8;
            /** @brief 입구 십자의 높이(m)입니다. */
            static constexpr float32 kGateHeight = 3.0f;

            static void appendBox( vector<EditorWorldSegment>& outListSegment, const float3& center, const float3& size, const float4& color )
            {
                const float32 halfX        = size._x * 0.5f;
                const float32 halfZ        = size._z * 0.5f;
                const float3  arrCorner[4] = {
                    center + float3{-halfX, 0.0f, -halfZ},
                    center + float3{ halfX, 0.0f, -halfZ},
                    center + float3{ halfX, 0.0f,  halfZ},
                    center + float3{-halfX, 0.0f,  halfZ}
                };
                const float3 up{ 0.0f, size._y, 0.0f };
                for ( size_t index = 0; index < 4; ++index )
                {
                    const float3& from = arrCorner[index];
                    const float3& to   = arrCorner[( index + 1 ) % 4];
                    outListSegment.push_back( EditorWorldSegment{ from, to, color } );
                    outListSegment.push_back( EditorWorldSegment{ from + up, to + up, color } );
                    outListSegment.push_back( EditorWorldSegment{ from, from + up, color } );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ParkLayoutPreview::ParkLayoutPreview()
        : _listRide{}
        , _listTrackPoint{}
        , _gatePosition{}
        , _layoutPath{}
        , _contentHash{ 0 }
        , _bLoaded{ false }
        , _bMissing{ false }
        , _bWarned{ false }
    {
    }

    void ParkLayoutPreview::clearPreview( bool bMissing )
    {
        _bLoaded  = false;
        _bMissing = bMissing;
        _listRide.clear();
        _listTrackPoint.clear();
    }

    bool ParkLayoutPreview::refresh( bool bForce )
    {
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        string        coasterPath;
        string        error;
        if ( GamePackDataUtil::tryMakeKindPath( packRoot, ParkLayout::kDataKind, _layoutPath, error ) == false ||
             GamePackDataUtil::tryMakeKindPath( packRoot, CoasterLayoutCatalog::kDataKind, coasterPath, error ) == false )
        {
            if ( _bWarned == false )
                SW_LOG_ERROR( "The park layout preview has no path in pack '%#': %#", packRoot.c_str(), error.c_str() );
            _bWarned    = true;
            _layoutPath = error;
            clearPreview( false );
            return false;
        }
        if ( AssetIO::hasResource( AssetID{ _layoutPath } ) == false )
        {
            clearPreview( true ); // 이 게임 팩에는 공원 배치가 없다 — 패널이 그 상태를 보인다
            return false;
        }
        string text;
        if ( AssetIO::readTextAsset( AssetID{ _layoutPath }, text ) == false )
        {
            if ( _bWarned == false )
                SW_LOG_WARNING( "%# could not be read - the park layout preview is empty", _layoutPath.c_str() );
            _bWarned = true;
            clearPreview( false );
            return false;
        }
        const uint64 contentHash       = HashUtil::computeStringHash64( text.data(), text.size(), false );
        const bool   bUnchangedContent = bForce == false && _bLoaded && contentHash == _contentHash;
        if ( bUnchangedContent )
            return false;
        _contentHash = contentHash;

        CoasterLayoutCatalog catalog;
        ParkLayout           layout;
        if ( catalog.loadFromAsset( AssetID{ coasterPath } ) == false )
        {
            SW_LOG_WARNING( "The coaster catalog '%#' could not be loaded - the park layout preview is empty", coasterPath );
            clearPreview( false );
            return true;
        }
        if ( layout.loadFromXMLText( text, catalog, _layoutPath ) == false )
        {
            SW_LOG_WARNING( "%# did not load through the ThemePark kit - the park layout preview is empty", _layoutPath.c_str() );
            clearPreview( false );
            return true;
        }

        _listRide.clear();
        _listTrackPoint.clear();
        _gatePosition = layout.getGatePosition();
        for ( const ParkRidePlacement& placement : layout.getPlacements() )
        {
            ParkRidePreview ride{};
            ride._name      = placement._ride._name;
            ride._position  = placement._position;
            ride._size      = placement._size;
            ride._color     = placement._color;
            ride._buildCost = placement._buildCost;
            ride._capacity  = placement._ride._capacity;
            ride._loadTime  = placement._loadTime;
            ride._bCoaster  = placement._layoutID.empty() == false;
            _listRide.push_back( ride );
            if ( ride._bCoaster == false )
                continue;
            const CoasterLayoutDef* pLayout = catalog.find( placement._layoutID );
            if ( pLayout == nullptr )
                continue;
            CoasterTrackBuilder builder;
            builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
            builder.appendPieces( pLayout->_listPiece );
            const CoasterTrack track = builder.makeTrack( true );
            vector<float3>     listPoint;
            for ( size_t index = 0; index < track.getPoints().size(); index += ParkLayoutPreviewInternal::kTrackPointStride )
            {
                listPoint.push_back( track.getPoints()[index]._position );
            }
            _listTrackPoint.push_back( std::move( listPoint ) );
        }
        _bLoaded  = true;
        _bMissing = false;
        _bWarned  = false;
        SW_LOG_INFO( "Park layout preview loaded %# rides from %#", static_cast<uint32>( _listRide.size() ), _layoutPath.c_str() );
        return true;
    }

    void ParkLayoutPreview::appendSegments( vector<EditorWorldSegment>& outListSegment ) const
    {
        for ( const ParkRidePreview& ride : _listRide )
        {
            if ( ride._bCoaster )
            {
                ParkLayoutPreviewInternal::appendBox( outListSegment, ride._position, float3{ 4.0f, 2.0f, 4.0f }, ride._color ); // 스테이션
                continue;
            }
            ParkLayoutPreviewInternal::appendBox( outListSegment, ride._position, ride._size, ride._color );
        }
        const float4 gateColor{ 1.0f, 0.9f, 0.4f, 1.0f };
        const float3 gateTop = _gatePosition + float3{ 0.0f, ParkLayoutPreviewInternal::kGateHeight, 0.0f };
        outListSegment.push_back( EditorWorldSegment{ _gatePosition, gateTop, gateColor } );
        outListSegment.push_back( EditorWorldSegment{
            gateTop + float3{-1.0f, 0.0f, 0.0f},
            gateTop + float3{ 1.0f, 0.0f, 0.0f},
            gateColor
        } );
        outListSegment.push_back( EditorWorldSegment{
            gateTop + float3{0.0f, 0.0f, -1.0f},
            gateTop + float3{0.0f, 0.0f,  1.0f},
            gateColor
        } );
        const float4 trackColor{ 0.8f, 0.8f, 0.85f, 1.0f };
        for ( const vector<float3>& listPoint : _listTrackPoint )
        {
            for ( size_t index = 1; index < listPoint.size(); ++index )
            {
                outListSegment.push_back( EditorWorldSegment{ listPoint[index - 1], listPoint[index], trackColor } );
            }
        }
    }

    SW_EDITOR_SUBSYSTEM( ParkLayoutPreview, 9300 );
} // namespace sw::editor
