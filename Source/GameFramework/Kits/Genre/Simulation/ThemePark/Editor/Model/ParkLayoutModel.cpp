#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/Model/ParkLayoutModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutModelInternal
        {
            /** @brief 트랙 선분을 이을 때 건너뛰는 점 간격입니다. 트랙 점은 0.25 m 마다 있어서 그대로 그리면 선분이 수천 개가 됩니다. */
            static constexpr size_t kTrackPointStride = 8;
            /** @brief 정문 십자의 높이(m)입니다. */
            static constexpr float32 kGateHeight = 3.0f;
            /** @brief 줄 입구 기둥의 높이(m)입니다. */
            static constexpr float32 kEntranceHeight = 1.5f;

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

            /** @brief 트랙을 그릴 점으로 솎습니다. 간격대로만 건너뛰면 끝 점들이 빠져 회로에 틈이 나므로, 끝 점과 (닫힌 회로면) 첫 점을 더합니다. */
            static void sampleTrack( const CoasterTrack& track, vector<float3>& outListPoint )
            {
                const vector<CoasterTrack::Point>& listPoint = track.getPoints();
                if ( listPoint.empty() )
                    return;
                for ( size_t index = 0; index < listPoint.size(); index += kTrackPointStride )
                {
                    outListPoint.push_back( listPoint[index]._position );
                }
                if ( ( listPoint.size() - 1 ) % kTrackPointStride != 0 )
                    outListPoint.push_back( listPoint.back()._position );
                if ( track.isClosed() )
                    outListPoint.push_back( listPoint.front()._position );
            }

            /** @brief 바닥 영역과 트랙을 모두 담는 범위를 잡습니다. */
            static void computeBounds( ParkRidePreview& ride )
            {
                const float3 half{ ride._size._x * 0.5f, 0.0f, ride._size._z * 0.5f };
                ride._boundsMin = ride._position - half;
                ride._boundsMax = ride._position + half + float3{ 0.0f, ride._size._y, 0.0f };
                for ( const float3& point : ride._listTrackPoint )
                {
                    ride._boundsMin = float3::min( ride._boundsMin, point );
                    ride._boundsMax = float3::max( ride._boundsMax, point );
                }
            }

            static bool isLess( const ParkRidePreview& left, const ParkRidePreview& right, ParkRideSortColumn column )
            {
                switch ( column )
                {
                    case ParkRideSortColumn::Name:
                        return string_view{ left._name } < string_view{ right._name };
                    case ParkRideSortColumn::BuildCost:
                        return left._buildCost < right._buildCost;
                    case ParkRideSortColumn::Capacity:
                        return left._capacity < right._capacity;
                    case ParkRideSortColumn::Time:
                        return left._timeSeconds < right._timeSeconds;
                    case ParkRideSortColumn::Price:
                        return left._price < right._price;
                    case ParkRideSortColumn::FileOrder:
                        return false;
                }
                return false;
            }
        };
    } // namespace

    float3 ParkRidePreview::computeFocusCenter() const
    {
        return ( _boundsMin + _boundsMax ) * 0.5f;
    }

    float32 ParkRidePreview::computeFocusRadius() const
    {
        return float3::getDistance( _boundsMin, _boundsMax ) * 0.5f;
    }

    bool ParkRidePreview::overlapsFootprint( const ParkRidePreview& other ) const
    {
        return MathUtil::abs( _position._x - other._position._x ) < ( _size._x + other._size._x ) * 0.5f && MathUtil::abs( _position._z - other._position._z ) < ( _size._z + other._size._z ) * 0.5f;
    }

    bool ParkRidePreview::isEntranceInsideFootprint() const
    {
        return _bCoaster == false && MathUtil::abs( _entrance._x - _position._x ) < _size._x * 0.5f && MathUtil::abs( _entrance._z - _position._z ) < _size._z * 0.5f;
    }

    ParkLayoutModel::ParkLayoutModel()
        : _listRide{}
        , _gatePosition{}
        , _contentHash{ 0 }
        , _selectedRide{ kNoRide }
        , _startingCash{ 0 }
        , _entryFee{ 0 }
        , _state{ ParkLayoutState::Unread }
    {
    }

    void ParkLayoutModel::clearPreview( ParkLayoutState state )
    {
        _state = state;
        _listRide.clear();
        _selectedRide = kNoRide;
        _contentHash  = 0; // 파일이 돌아오면 같은 글이어도 다시 읽는다
    }

    void ParkLayoutModel::setMissing()
    {
        clearPreview( ParkLayoutState::Missing );
    }

    void ParkLayoutModel::setFailed()
    {
        clearPreview( ParkLayoutState::Failed );
    }

    bool ParkLayoutModel::applyTexts( string_view layoutText, string_view coasterText, string_view layoutPath, string_view coasterPath, bool bForce )
    {
        const uint64 contentHash       = HashUtil::computeStringHash64( coasterText, false, HashUtil::computeStringHash64( layoutText, false ) );
        const bool   bAlreadyRead      = _state == ParkLayoutState::Loaded || ( _state == ParkLayoutState::Failed && _contentHash != 0 );
        const bool   bUnchangedContent = bForce == false && bAlreadyRead && contentHash == _contentHash;
        if ( bUnchangedContent )
            return false;

        CoasterLayoutCatalog catalog;
        ParkLayout           layout;
        if ( catalog.loadFromXMLText( coasterText, coasterPath ) == false )
        {
            SW_LOG_WARNING_ID( "ParkLayoutModel.CoasterCatalogCouldNot", "The coaster catalog '%#' could not be loaded - the park layout preview is empty", coasterPath );
            clearPreview( ParkLayoutState::Failed );
            _contentHash = contentHash;
            return true;
        }
        if ( layout.loadFromXMLText( layoutText, catalog, layoutPath ) == false )
        {
            SW_LOG_WARNING_ID( "ParkLayoutModel.DidNotLoadThrough", "%# did not load through the ThemePark kit - the park layout preview is empty", layoutPath );
            clearPreview( ParkLayoutState::Failed );
            _contentHash = contentHash;
            return true;
        }
        rebuild( layout, catalog );
        _contentHash = contentHash;
        _state       = ParkLayoutState::Loaded;
        SW_LOG_INFO_ID( "ParkLayoutModel.ParkLayoutPreviewLoaded", "Park layout preview loaded %# rides from %#", static_cast<uint32>( _listRide.size() ), layoutPath );
        return true;
    }

    void ParkLayoutModel::rebuild( const ParkLayout& layout, const CoasterLayoutCatalog& catalog )
    {
        const bool          bHadSelection = _selectedRide < _listRide.size();
        const hashed_string selectedID    = bHadSelection ? _listRide[_selectedRide]._id : hashed_string{};
        const string        selectedName  = bHadSelection ? _listRide[_selectedRide]._name : string{};

        _listRide.clear();
        _selectedRide = kNoRide;
        _gatePosition = layout.getGatePosition();
        _startingCash = layout.getStartingCash();
        _entryFee     = layout.getEntryFee();
        for ( const ParkRidePlacement& placement : layout.getPlacements() )
        {
            ParkRidePreview ride{};
            ride._name      = placement._ride._name;
            ride._id        = placement._ride._id;
            ride._position  = placement._position;
            ride._color     = placement._color;
            ride._buildCost = placement._buildCost;
            ride._capacity  = placement._ride._capacity;
            ride._bCoaster  = placement._layoutID.empty() == false;
            if ( ride._bCoaster )
            {
                ride._size                      = kStationSize;
                ride._timeSeconds               = placement._loadTime;
                const CoasterLayoutDef* pLayout = catalog.find( placement._layoutID );
                if ( pLayout != nullptr ) // `ParkLayout` 이 없는 레이아웃을 이미 거절했다
                {
                    CoasterTrackBuilder builder;
                    builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
                    builder.appendPieces( pLayout->_listPiece );
                    ParkLayoutModelInternal::sampleTrack( builder.makeTrack( true ), ride._listTrackPoint );
                }
            }
            else
            {
                ride._size        = placement._size;
                ride._entrance    = placement._ride._entrance;
                ride._price       = placement._ride._price;
                ride._timeSeconds = placement._ride._cycleTime; // `_loadTime` 은 코스터 칸이다 — 평지 놀이기구는 기본값 그대로다
            }
            ParkLayoutModelInternal::computeBounds( ride );
            if ( bHadSelection && _selectedRide == kNoRide && ride._id == selectedID && ride._name == selectedName )
                _selectedRide = static_cast<uint32>( _listRide.size() );
            _listRide.push_back( std::move( ride ) );
        }
        rebuildIssues();
    }

    void ParkLayoutModel::rebuildIssues()
    {
        const uint32 rideCount = static_cast<uint32>( _listRide.size() );
        for ( uint32 rideIndex = 0; rideIndex < rideCount; ++rideIndex )
        {
            ParkRidePreview& ride = _listRide[rideIndex];
            if ( ride.isEntranceInsideFootprint() )
                ride._listIssue.push_back( ParkRideIssue{ ParkRideIssueKind::EntranceInsideFootprint, rideIndex } );
            for ( uint32 otherIndex = rideIndex + 1; otherIndex < rideCount; ++otherIndex )
            {
                ParkRidePreview& other = _listRide[otherIndex];
                if ( ride.overlapsFootprint( other ) )
                {
                    ride._listIssue.push_back( ParkRideIssue{ ParkRideIssueKind::Overlap, otherIndex } );
                    other._listIssue.push_back( ParkRideIssue{ ParkRideIssueKind::Overlap, rideIndex } );
                }
                if ( ride._id == other._id )
                {
                    ride._listIssue.push_back( ParkRideIssue{ ParkRideIssueKind::DuplicateID, otherIndex } );
                    other._listIssue.push_back( ParkRideIssue{ ParkRideIssueKind::DuplicateID, rideIndex } );
                }
            }
        }
    }

    int64 ParkLayoutModel::computeTotalBuildCost() const
    {
        int64 total = 0;
        for ( const ParkRidePreview& ride : _listRide )
        {
            total += ride._buildCost;
        }
        return total;
    }

    uint32 ParkLayoutModel::countIssues() const
    {
        uint32 count = 0;
        for ( const ParkRidePreview& ride : _listRide )
        {
            count += static_cast<uint32>( ride._listIssue.size() );
        }
        return count;
    }

    void ParkLayoutModel::collectRows( const EditorListFilter& filter, ParkRideSortColumn column, bool bDescending, vector<uint32>& outListRide ) const
    {
        outListRide.clear();
        for ( uint32 rideIndex = 0; rideIndex < static_cast<uint32>( _listRide.size() ); ++rideIndex )
        {
            const ParkRidePreview& ride = _listRide[rideIndex];
            if ( filter.matchesAny( { string_view{ ride._name }, string_view{ ride._id.c_str() } } ) )
                outListRide.push_back( rideIndex );
        }
        if ( column == ParkRideSortColumn::FileOrder )
            return;
        VectorUtil::stableSort( outListRide, [this, column, bDescending]( uint32 left, uint32 right )
        {
            const ParkRidePreview& leftRide  = _listRide[left];
            const ParkRidePreview& rightRide = _listRide[right];
            return bDescending ? ParkLayoutModelInternal::isLess( rightRide, leftRide, column ) : ParkLayoutModelInternal::isLess( leftRide, rightRide, column );
        } );
    }

    void ParkLayoutModel::selectRide( uint32 rideIndex )
    {
        _selectedRide = rideIndex < _listRide.size() ? rideIndex : kNoRide;
    }

    uint32 ParkLayoutModel::findNearestRide( const float3& position, float32& outDistance ) const
    {
        uint32 nearest = kNoRide;
        outDistance    = 0.0f;
        for ( uint32 rideIndex = 0; rideIndex < static_cast<uint32>( _listRide.size() ); ++rideIndex )
        {
            const float32 distance = float3::getHorizontalDistance( _listRide[rideIndex]._position, position );
            if ( nearest == kNoRide || distance < outDistance )
            {
                nearest     = rideIndex;
                outDistance = distance;
            }
        }
        return nearest;
    }

    void ParkLayoutModel::appendSegments( const ParkLayoutColors& colors, vector<EditorWorldSegment>& outListSegment ) const
    {
        for ( uint32 rideIndex = 0; rideIndex < static_cast<uint32>( _listRide.size() ); ++rideIndex )
        {
            const ParkRidePreview& ride      = _listRide[rideIndex];
            const bool             bSelected = rideIndex == _selectedRide;
            const float4&          rideColor = bSelected ? colors._selected : ride._color;
            ParkLayoutModelInternal::appendBox( outListSegment, ride._position, ride._size, rideColor );
            if ( ride._bCoaster == false )
            {
                const float3 entranceTop = ride._entrance + float3{ 0.0f, ParkLayoutModelInternal::kEntranceHeight, 0.0f };
                outListSegment.push_back( EditorWorldSegment{ ride._entrance, entranceTop, rideColor } );
            }
            const float4& trackColor = bSelected ? colors._selected : colors._track;
            for ( size_t pointIndex = 1; pointIndex < ride._listTrackPoint.size(); ++pointIndex )
            {
                outListSegment.push_back( EditorWorldSegment{ ride._listTrackPoint[pointIndex - 1], ride._listTrackPoint[pointIndex], trackColor } );
            }
        }
        const float3 gateTop = _gatePosition + float3{ 0.0f, ParkLayoutModelInternal::kGateHeight, 0.0f };
        outListSegment.push_back( EditorWorldSegment{ _gatePosition, gateTop, colors._gate } );
        outListSegment.push_back( EditorWorldSegment{
            gateTop + float3{-1.0f, 0.0f, 0.0f},
            gateTop + float3{ 1.0f, 0.0f, 0.0f},
            colors._gate
        } );
        outListSegment.push_back( EditorWorldSegment{
            gateTop + float3{0.0f, 0.0f, -1.0f},
            gateTop + float3{0.0f, 0.0f,  1.0f},
            colors._gate
        } );
    }
} // namespace sw::editor
