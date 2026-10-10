#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ParkLayoutPreview" );

    namespace
    {
        struct ParkLayoutPreviewInternal
        {
            /** @brief 트랙 선분을 이을 때 건너뛰는 점 간격입니다. 트랙 점은 0.25 m 마다라 그대로 그리면 선분이 수천 개다. */
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
        , _contentHash{ 0 }
        , _bLoaded{ false }
        , _bWarned{ false }
    {
    }

    bool ParkLayoutPreview::refresh( bool bForce )
    {
        string text;
        if ( ResourceUtil::readTextResource( kLayoutPath, text ) == false )
        {
            if ( _bWarned == false )
                SW_LOG_WARNING( "%# could not be read - the park layout preview is empty", kLayoutPath );
            _bWarned = true;
            _bLoaded = false;
            _listRide.clear();
            _listTrackPoint.clear();
            return false;
        }
        const uint64 contentHash = StringUtil::computeHash64( text.data(), text.size(), false );
        if ( bForce == false && _bLoaded && contentHash == _contentHash )
            return false;
        _contentHash = contentHash;

        CoasterLayoutCatalog catalog;
        ParkLayout           layout;
        if ( catalog.loadFromResource( kCoasterPath ) == false || layout.loadFromXMLText( text, catalog, kLayoutPath ) == false )
        {
            SW_LOG_WARNING( "%# did not load through the ThemePark kit - the park layout preview is empty", kLayoutPath );
            _bLoaded = false;
            _listRide.clear();
            _listTrackPoint.clear();
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
            const CoasterLayoutDef* pLayout = catalog.findLayout( placement._layoutID );
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
        _bLoaded = true;
        _bWarned = false;
        SW_LOG_INFO( "Park layout preview loaded %# rides from %#", static_cast<uint32>( _listRide.size() ), kLayoutPath );
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

    ParkLayoutPreview& ParkLayoutPreview::get()
    {
        static ParkLayoutPreview s_preview;
        return s_preview;
    }
} // namespace sw::editor
