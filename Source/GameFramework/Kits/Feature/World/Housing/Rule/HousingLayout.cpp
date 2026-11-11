#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HousingLayoutInternal
        {
            static constexpr int32   kLayerCount   = 3;       ///< `HousingPartLayer` 의 수
            static constexpr float32 kCellEpsilon  = 1.0e-3f; ///< 칸 경계에 닿은 상자는 그 칸을 덮지 않는다(칸 단위)
            static constexpr float32 kRayEpsilon   = 1.0e-6f;
            static constexpr uint32  kPlacementMin = 4 + 4 + 4 + 4 * 4; ///< 저장된 배치 하나의 최소 바이트(id · 빈 이름 길이 · 판 · 변환)

            /** @brief 요를 [0, 2π) 로 접습니다. 같은 각이 같은 바이트로 저장되게 한다. */
            static float32 wrapYaw( float32 yaw )
            {
                float32 wrapped = yaw - MathUtil::kTwoPi * MathUtil::floor( yaw / MathUtil::kTwoPi );
                return wrapped >= MathUtil::kTwoPi ? 0.0f : wrapped;
            }

            /**
             * @brief 부품 기준 XZ 를 요 @p yaw 만큼 돌립니다. `SceneComponent` 의 요(Y 축, 왼손 좌표 — DirectX `RotationY`)와 같은 방향이다.
             * @details 프리팹이 회전 0 으로 저작돼 뷰가 같은 요를 루트에 넣으므로, 소켓도 같은 방향으로 돌아야 그림과 스냅이 맞는다.
             */
            static float2 rotate( const float2& local, float32 yaw )
            {
                const float32 c = MathUtil::cos( yaw );
                const float32 s = MathUtil::sin( yaw );
                return float2{ local._x * c + local._y * s, -local._x * s + local._y * c };
            }

            /** @brief 받침을 잃는지 볼 상대가 놓인 층입니다. 바닥은 같은 층, 구조물은 위층을 받친다. 가구는 아무것도 받치지 않는다(-1). */
            static int32 computeSupportedLevel( const HousingPartDef& part, int32 level )
            {
                switch ( part._layer )
                {
                    case HousingPartLayer::Floor:
                        return level;
                    case HousingPartLayer::Structure:
                        return level + 1;
                    case HousingPartLayer::Furniture:
                        return constant::invalid_index::kInt32;
                }
                return constant::invalid_index::kInt32;
            }

            /** @brief @p layer 의 부품이 @p other 층의 칸과 겹칠 수 없으면 true 입니다(바닥끼리, 구조물 · 가구끼리). */
            static bool isConflicting( HousingPartLayer layer, HousingPartLayer other )
            {
                const bool bFloor      = layer == HousingPartLayer::Floor;
                const bool bOtherFloor = other == HousingPartLayer::Floor;
                return bFloor == bOtherFloor;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void HousingTransform::serializeState( StateIO& io )
    {
        io.value( _position ); // 실수의 NaN · 무한은 아카이브 읽기가 거절한다
        io.value( _level );
        io.value( _yaw );
        io.check( _level >= 0 && _yaw >= 0.0f && _yaw < MathUtil::kTwoPi );
    }

    void HousingPlacement::serializeState( StateIO& io )
    {
        io.value( _placementID );
        io.value( _partID );
        io.value( _partVersion );
        io.value( _transform );
        io.check( _placementID != 0 && _partID.empty() == false );
    }
} // namespace sw

namespace sw
{
    HousingLayout::HousingLayout()
        : _settings{}
        , _pCatalog{ nullptr }
        , _pWallet{ nullptr }
        , _land{}
        , _listPlacement{}
        , _listCellOwner{}
        , _listEdit{}
        , _editCursor{ 0 }
        , _nextPlacementID{ 1 }
        , _revision{ 0 }
    {
    }

    void HousingLayout::initialize( const HousingPlotSettings& settings, const HousingPartCatalog& catalog )
    {
        _settings = settings;
        _pCatalog = &catalog;
        _listPlacement.clear();
        _listCellOwner.assign( static_cast<size_t>( HousingLayoutInternal::kLayerCount * settings._levelCount * settings._sizeX * settings._sizeZ ), 0u );
        _listEdit.clear();
        _editCursor      = 0;
        _nextPlacementID = 1;
        ++_revision;
    }

    bool HousingLayout::bindLand( LandRegistry* pLand, const int2& origin )
    {
        releaseLand();
        if ( pLand == nullptr )
            return true;
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "Housing." + string( _settings._plotID.c_str() ) ) );
        if ( land.claimRect( 0, 0, _settings._sizeX - 1, _settings._sizeZ - 1, false ) == false )
        {
            SW_LOG_ERROR_ID( "HousingLayout.LandTaken", "Housing plot '%#': its %#x%# cells at land cell %#,%# are outside the land or used by another kit", _settings._plotID.c_str(), _settings._sizeX,
                             _settings._sizeZ, origin._x, origin._y );
            return false;
        }
        _land = land;
        return true;
    }

    void HousingLayout::releaseLand()
    {
        _land.releaseRect( 0, 0, _settings._sizeX - 1, _settings._sizeZ - 1 );
        _land = LandBinding{};
    }

    const HousingPlacement* HousingLayout::findPlacement( uint32 placementID ) const
    {
        const auto iter = std::lower_bound( _listPlacement.begin(), _listPlacement.end(), placementID,
                                            []( const HousingPlacement& placement, uint32 id )
        { return placement._placementID < id; } );
        return iter != _listPlacement.end() && iter->_placementID == placementID ? &*iter : nullptr;
    }

    float3 HousingLayout::computeLocalPosition( const HousingTransform& transform ) const
    {
        const float32 levelHeight = _pCatalog != nullptr ? _pCatalog->getRoot()._levelHeight : 0.0f;
        return float3{ transform._position._x, static_cast<float32>( transform._level ) * levelHeight, transform._position._y };
    }

    bool HousingLayout::projectRay( const float3& localOrigin, const float3& direction, int32 level, float2& outPosition ) const
    {
        if ( _pCatalog == nullptr || MathUtil::abs( direction._y ) < HousingLayoutInternal::kRayEpsilon )
            return false;
        const float32 planeY   = static_cast<float32>( level ) * _pCatalog->getRoot()._levelHeight;
        const float32 distance = ( planeY - localOrigin._y ) / direction._y;
        if ( distance < 0.0f )
            return false;
        outPosition = float2{ localOrigin._x + direction._x * distance, localOrigin._z + direction._z * distance };
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스냅과 검증
    // ------------------------------------------------------------------------------
    HousingPlaceCheck HousingLayout::snap( const HousingPartDef& part, const HousingCandidate& candidate, uint32 ignoreID ) const
    {
        using Internal = HousingLayoutInternal;
        HousingPlaceCheck check;
        check._transform = candidate._transform;
        if ( candidate._mode == HousingPlacementMode::Free && ( _settings._bFreePlacement == false || part._bFreePlacement == false ) )
        {
            check._result = HousingPlaceResult::FreePlacementNotAllowed;
            return check;
        }
        const HousingPartCatalogRootDef& root = _pCatalog->getRoot();
        // 소켓: 붙는 부품의 같은 종류 소켓(없으면 원점)을 대상 소켓에 맞춘다(발하임 스냅 점과 같은 점 대 점). 날 자리에서 가장 가까운 짝이 반지름 안이면 이긴다.
        if ( part._snapKind.empty() == false )
        {
            float32 bestDistanceSq = root._socketSnapRadius * root._socketSnapRadius;
            for ( const HousingPlacement& target : _listPlacement )
            {
                const HousingPartDef* pTargetPart = target._placementID != ignoreID && target._transform._level == candidate._transform._level ? _pCatalog->findPart( target._partID ) : nullptr;
                if ( pTargetPart == nullptr )
                    continue;
                for ( const HousingSocketDef& targetSocket : pTargetPart->_listSocket )
                {
                    if ( targetSocket._kind != part._snapKind )
                        continue;
                    const float2  socketPosition = target._transform._position + Internal::rotate( float2{ targetSocket._position._x, targetSocket._position._z }, target._transform._yaw );
                    const float32 yaw            = Internal::wrapYaw( target._transform._yaw + static_cast<float32>( targetSocket._quarterTurns ) * MathUtil::kHalfPi );
                    bool          bOwnSocket     = false;
                    for ( const HousingSocketDef& ownSocket : part._listSocket )
                    {
                        if ( ownSocket._kind != part._snapKind )
                            continue;
                        bOwnSocket           = true;
                        const float2  center = socketPosition - Internal::rotate( float2{ ownSocket._position._x, ownSocket._position._z }, yaw );
                        const float2  offset = center - candidate._transform._position;
                        const float32 distSq = offset._x * offset._x + offset._y * offset._y;
                        if ( distSq > bestDistanceSq )
                            continue;
                        bestDistanceSq           = distSq;
                        check._transform         = HousingTransform{ center, candidate._transform._level, yaw };
                        check._socketPlacementID = target._placementID;
                    }
                    const float2  offset = socketPosition - candidate._transform._position;
                    const float32 distSq = offset._x * offset._x + offset._y * offset._y;
                    if ( bOwnSocket || distSq > bestDistanceSq )
                        continue;
                    bestDistanceSq           = distSq;
                    check._transform         = HousingTransform{ socketPosition, candidate._transform._level, yaw };
                    check._socketPlacementID = target._placementID;
                }
            }
            if ( check._socketPlacementID != 0 )
                return check;
        }
        if ( candidate._mode == HousingPlacementMode::Free )
        {
            check._transform._yaw = Internal::wrapYaw( check._transform._yaw );
            return check;
        }
        // 격자: 90 도로 맞추고, 돌린 점유 칸의 왼쪽 아래 칸을 반올림해 가운데를 칸에 맞춘다.
        const int32   quarterTurns = ( static_cast<int32>( MathUtil::round( check._transform._yaw / MathUtil::kHalfPi ) ) % 4 + 4 ) % 4;
        const bool    bSwapped     = ( quarterTurns % 2 ) == 1;
        const float32 sizeX        = static_cast<float32>( bSwapped ? part._sizeZ : part._sizeX );
        const float32 sizeZ        = static_cast<float32>( bSwapped ? part._sizeX : part._sizeZ );
        const float32 cellMinX     = MathUtil::round( check._transform._position._x / root._cellSize - sizeX * 0.5f );
        const float32 cellMinZ     = MathUtil::round( check._transform._position._y / root._cellSize - sizeZ * 0.5f );
        check._transform._position = float2{ ( cellMinX + sizeX * 0.5f ) * root._cellSize, ( cellMinZ + sizeZ * 0.5f ) * root._cellSize };
        check._transform._yaw      = static_cast<float32>( quarterTurns ) * MathUtil::kHalfPi;
        return check;
    }

    void HousingLayout::computeCells( const HousingPartDef& part, const HousingTransform& transform, int2& outMin, int2& outMax ) const
    {
        using Internal         = HousingLayoutInternal;
        const float32 cellSize = _pCatalog->getRoot()._cellSize;
        const float32 halfX    = static_cast<float32>( part._sizeX ) * cellSize * 0.5f;
        const float32 halfZ    = static_cast<float32>( part._sizeZ ) * cellSize * 0.5f;
        const float32 c        = MathUtil::abs( MathUtil::cos( transform._yaw ) );
        const float32 s        = MathUtil::abs( MathUtil::sin( transform._yaw ) );
        const float32 extentX  = c * halfX + s * halfZ;
        const float32 extentZ  = s * halfX + c * halfZ;
        const float2& center   = transform._position;
        outMin                 = int2{ static_cast<int32>( MathUtil::floor( ( center._x - extentX ) / cellSize + Internal::kCellEpsilon ) ),
                       static_cast<int32>( MathUtil::floor( ( center._y - extentZ ) / cellSize + Internal::kCellEpsilon ) ) };
        outMax                 = int2{ static_cast<int32>( MathUtil::floor( ( center._x + extentX ) / cellSize - Internal::kCellEpsilon ) ),
                       static_cast<int32>( MathUtil::floor( ( center._y + extentZ ) / cellSize - Internal::kCellEpsilon ) ) };
    }

    size_t HousingLayout::computeCellIndex( HousingPartLayer layer, int32 level, int32 x, int32 z ) const
    {
        const size_t levelIndex = static_cast<size_t>( layer ) * static_cast<size_t>( _settings._levelCount ) + static_cast<size_t>( level );
        return ( levelIndex * static_cast<size_t>( _settings._sizeZ ) + static_cast<size_t>( z ) ) * static_cast<size_t>( _settings._sizeX ) + static_cast<size_t>( x );
    }

    uint32 HousingLayout::findOwner( HousingPartLayer layer, int32 level, int32 x, int32 z ) const
    {
        return _listCellOwner[computeCellIndex( layer, level, x, z )];
    }

    void HousingLayout::occupy( const HousingPlacement& placement, uint32 owner )
    {
        const HousingPartDef* pPart = _pCatalog->findPart( placement._partID );
        int2                  cellMin;
        int2                  cellMax;
        computeCells( *pPart, placement._transform, cellMin, cellMax );
        for ( int32 z = cellMin._y; z <= cellMax._y; ++z )
        {
            for ( int32 x = cellMin._x; x <= cellMax._x; ++x )
            {
                _listCellOwner[computeCellIndex( pPart->_layer, placement._transform._level, x, z )] = owner;
            }
        }
    }

    bool HousingLayout::isSupported( const HousingPartDef& part, int32 level, const int2& cellMin, const int2& cellMax, uint32 ignoreID ) const
    {
        const bool bFloor = part._layer == HousingPartLayer::Floor;
        if ( bFloor && level == 0 )
            return true; // 땅
        for ( int32 z = cellMin._y; z <= cellMax._y; ++z )
        {
            for ( int32 x = cellMin._x; x <= cellMax._x; ++x )
            {
                const uint32 owner  = bFloor ? findOwner( HousingPartLayer::Structure, level - 1, x, z ) : findOwner( HousingPartLayer::Floor, level, x, z );
                const bool   bHolds = owner != 0 && owner != ignoreID;
                if ( bFloor && bHolds )
                    return true; // 위층 바닥은 벽 하나에 걸치면 선다
                if ( bFloor == false && bHolds == false )
                    return false; // 벽 · 가구는 모든 칸이 바닥 위다
            }
        }
        return bFloor == false;
    }

    HousingPlaceResult HousingLayout::checkFootprint( const HousingPartDef& part, const HousingTransform& transform, uint32 ignoreID, int2& outMin, int2& outMax ) const
    {
        using Internal = HousingLayoutInternal;
        if ( transform._level < 0 || transform._level >= _settings._levelCount )
            return HousingPlaceResult::BadLevel;
        computeCells( part, transform, outMin, outMax );
        if ( outMin._x < 0 || outMin._y < 0 || outMax._x >= _settings._sizeX || outMax._y >= _settings._sizeZ )
            return HousingPlaceResult::OutOfPlot;
        for ( int32 layerIndex = 0; layerIndex < Internal::kLayerCount; ++layerIndex )
        {
            const HousingPartLayer layer = static_cast<HousingPartLayer>( layerIndex );
            if ( Internal::isConflicting( part._layer, layer ) == false )
                continue;
            for ( int32 z = outMin._y; z <= outMax._y; ++z )
            {
                for ( int32 x = outMin._x; x <= outMax._x; ++x )
                {
                    const uint32 owner = findOwner( layer, transform._level, x, z );
                    if ( owner != 0 && owner != ignoreID )
                        return HousingPlaceResult::Overlap;
                }
            }
        }
        return isSupported( part, transform._level, outMin, outMax, ignoreID ) ? HousingPlaceResult::Ok : HousingPlaceResult::NoSupport;
    }

    bool HousingLayout::supportsOthers( const HousingPlacement& placement ) const
    {
        using Internal                  = HousingLayoutInternal;
        const HousingPartDef* pPart     = _pCatalog->findPart( placement._partID );
        const int32           heldLevel = Internal::computeSupportedLevel( *pPart, placement._transform._level );
        if ( heldLevel < 0 )
            return false;
        for ( const HousingPlacement& other : _listPlacement )
        {
            if ( other._placementID == placement._placementID || other._transform._level != heldLevel )
                continue;
            const HousingPartDef* pOther = _pCatalog->findPart( other._partID );
            // 같은 층의 바닥끼리 · 위층의 벽 · 가구는 이 부품에 기대지 않는다.
            const bool bLeans = pPart->_layer == HousingPartLayer::Floor ? pOther->_layer != HousingPartLayer::Floor : pOther->_layer == HousingPartLayer::Floor;
            if ( bLeans == false )
                continue;
            int2 cellMin;
            int2 cellMax;
            computeCells( *pOther, other._transform, cellMin, cellMax );
            if ( isSupported( *pOther, other._transform._level, cellMin, cellMax, placement._placementID ) == false )
                return true;
        }
        return false;
    }

    HousingPlaceCheck HousingLayout::evaluate( const HousingCandidate& candidate ) const
    {
        const HousingPartDef* pPart = _pCatalog != nullptr ? _pCatalog->findPart( candidate._partID ) : nullptr;
        if ( pPart == nullptr )
        {
            HousingPlaceCheck check;
            check._result    = HousingPlaceResult::UnknownPart;
            check._transform = candidate._transform;
            return check;
        }
        HousingPlaceCheck check = snap( *pPart, candidate, 0 );
        check._cost             = pPart->_cost;
        if ( check._result != HousingPlaceResult::Ok )
            return check;
        check._result = checkFootprint( *pPart, check._transform, 0, check._cellMin, check._cellMax );
        if ( check._result != HousingPlaceResult::Ok || pPart->_cost == 0 )
            return check;
        if ( _pWallet == nullptr )
            check._result = HousingPlaceResult::NoWallet;
        else if ( _pWallet->canAfford( _pCatalog->getRoot()._currency, pPart->_cost ) == false )
            check._result = HousingPlaceResult::NotEnoughFunds;
        return check;
    }

    // ------------------------------------------------------------------------------
    // 편집 — 놓기 · 치우기 · 옮기기와 그 역
    // ------------------------------------------------------------------------------
    HousingPlaceResult HousingLayout::tryInsert( const HousingPlacement& placement, int64 charge )
    {
        const HousingPartDef* pPart = _pCatalog->findPart( placement._partID );
        if ( pPart == nullptr )
            return HousingPlaceResult::UnknownPart; // 이력의 부품이 다시 읽은 카탈로그에서 빠졌다
        int2                     cellMin;
        int2                     cellMax;
        const HousingPlaceResult result = checkFootprint( *pPart, placement._transform, 0, cellMin, cellMax );
        if ( result != HousingPlaceResult::Ok )
            return result;
        if ( charge > 0 )
        {
            if ( _pWallet == nullptr )
                return HousingPlaceResult::NoWallet;
            if ( _pWallet->trySpend( _pCatalog->getRoot()._currency, charge ) == false )
                return HousingPlaceResult::NotEnoughFunds;
        }
        const auto iter = std::lower_bound( _listPlacement.begin(), _listPlacement.end(), placement._placementID,
                                            []( const HousingPlacement& existing, uint32 id )
        { return existing._placementID < id; } );
        _listPlacement.insert( iter, placement );
        occupy( placement, placement._placementID );
        ++_revision;
        return HousingPlaceResult::Ok;
    }

    HousingPlaceResult HousingLayout::tryErase( uint32 placementID, int64 refund )
    {
        const HousingPlacement* pPlacement = findPlacement( placementID );
        if ( pPlacement == nullptr )
            return HousingPlaceResult::UnknownPlacement;
        if ( _pCatalog->findPart( pPlacement->_partID ) == nullptr )
            return HousingPlaceResult::UnknownPart;
        if ( supportsOthers( *pPlacement ) )
            return HousingPlaceResult::SupportsOthers;
        if ( refund > 0 )
        {
            if ( _pWallet == nullptr )
                return HousingPlaceResult::NoWallet;
            _pWallet->add( _pCatalog->getRoot()._currency, refund );
        }
        occupy( *pPlacement, 0 );
        _listPlacement.erase( _listPlacement.begin() + ( pPlacement - _listPlacement.data() ) );
        ++_revision;
        return HousingPlaceResult::Ok;
    }

    HousingPlaceResult HousingLayout::tryRelocate( uint32 placementID, const HousingTransform& transform )
    {
        const HousingPlacement* pPlacement = findPlacement( placementID );
        if ( pPlacement == nullptr )
            return HousingPlaceResult::UnknownPlacement;
        const HousingPartDef* pPart = _pCatalog->findPart( pPlacement->_partID );
        if ( pPart == nullptr )
            return HousingPlaceResult::UnknownPart;
        if ( supportsOthers( *pPlacement ) )
            return HousingPlaceResult::SupportsOthers;
        int2                     cellMin;
        int2                     cellMax;
        const HousingPlaceResult result = checkFootprint( *pPart, transform, placementID, cellMin, cellMax );
        if ( result != HousingPlaceResult::Ok )
            return result;
        HousingPlacement& moved = _listPlacement[static_cast<size_t>( pPlacement - _listPlacement.data() )];
        occupy( moved, 0 );
        moved._transform = transform;
        occupy( moved, placementID );
        ++_revision;
        return HousingPlaceResult::Ok;
    }

    void HousingLayout::recordEdit( const Edit& edit )
    {
        _listEdit.resize( _editCursor ); // 새 편집은 다시 할 것을 버린다
        if ( _listEdit.size() >= static_cast<size_t>( _settings._historyLimit ) )
            _listEdit.erase( _listEdit.begin() );
        _listEdit.push_back( edit );
        _editCursor = static_cast<uint32>( _listEdit.size() );
    }

    HousingPlaceResult HousingLayout::place( const HousingCandidate& candidate, uint32& outPlacementID )
    {
        const HousingPlaceCheck check = evaluate( candidate );
        if ( check._result != HousingPlaceResult::Ok )
            return check._result;
        HousingPlacement placement;
        placement._placementID          = _nextPlacementID;
        placement._partID               = candidate._partID;
        placement._partVersion          = _pCatalog->findPart( candidate._partID )->_version;
        placement._transform            = check._transform;
        const HousingPlaceResult result = tryInsert( placement, check._cost );
        if ( result != HousingPlaceResult::Ok )
            return result;
        ++_nextPlacementID;
        Edit edit;
        edit._kind   = EditKind::Place;
        edit._after  = placement;
        edit._amount = check._cost;
        recordEdit( edit );
        outPlacementID = placement._placementID;
        return HousingPlaceResult::Ok;
    }

    HousingPlaceResult HousingLayout::remove( uint32 placementID )
    {
        const HousingPlacement* pPlacement = findPlacement( placementID );
        if ( pPlacement == nullptr )
            return HousingPlaceResult::UnknownPlacement;
        const HousingPartDef* pPart = _pCatalog->findPart( pPlacement->_partID );
        if ( pPart == nullptr )
            return HousingPlaceResult::UnknownPart;
        const HousingPlacement   removed = *pPlacement; // 지우면 포인터가 무효다
        const int64              refund  = static_cast<int64>( MathUtil::floor( static_cast<float32>( pPart->_cost ) * pPart->_refundRatio ) );
        const HousingPlaceResult result  = tryErase( placementID, refund );
        if ( result != HousingPlaceResult::Ok )
            return result;
        Edit edit;
        edit._kind   = EditKind::Remove;
        edit._before = removed;
        edit._amount = refund;
        recordEdit( edit );
        return HousingPlaceResult::Ok;
    }

    HousingPlaceResult HousingLayout::move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode )
    {
        const HousingPlacement* pPlacement = findPlacement( placementID );
        if ( pPlacement == nullptr )
            return HousingPlaceResult::UnknownPlacement;
        const HousingPartDef* pPart = _pCatalog->findPart( pPlacement->_partID );
        if ( pPart == nullptr )
            return HousingPlaceResult::UnknownPart;
        HousingCandidate candidate;
        candidate._partID             = pPlacement->_partID;
        candidate._transform          = rawTransform;
        candidate._mode               = mode;
        const HousingPlaceCheck check = snap( *pPart, candidate, placementID );
        if ( check._result != HousingPlaceResult::Ok )
            return check._result;
        const HousingPlacement   before = *pPlacement;
        const HousingPlaceResult result = tryRelocate( placementID, check._transform );
        if ( result != HousingPlaceResult::Ok )
            return result;
        Edit edit;
        edit._kind             = EditKind::Move;
        edit._before           = before;
        edit._after            = before;
        edit._after._transform = check._transform;
        recordEdit( edit );
        return HousingPlaceResult::Ok;
    }

    HousingPlaceResult HousingLayout::undo()
    {
        if ( canUndo() == false )
            return HousingPlaceResult::NothingToUndo;
        const Edit&        edit   = _listEdit[_editCursor - 1];
        HousingPlaceResult result = HousingPlaceResult::Ok;
        switch ( edit._kind )
        {
            case EditKind::Place:
                result = tryErase( edit._after._placementID, edit._amount );
                break;
            case EditKind::Remove:
                result = tryInsert( edit._before, edit._amount );
                break;
            case EditKind::Move:
                result = tryRelocate( edit._before._placementID, edit._before._transform );
                break;
        }
        if ( result == HousingPlaceResult::Ok )
            --_editCursor;
        return result;
    }

    HousingPlaceResult HousingLayout::redo()
    {
        if ( canRedo() == false )
            return HousingPlaceResult::NothingToRedo;
        const Edit&        edit   = _listEdit[_editCursor];
        HousingPlaceResult result = HousingPlaceResult::Ok;
        switch ( edit._kind )
        {
            case EditKind::Place:
                result = tryInsert( edit._after, edit._amount );
                break;
            case EditKind::Remove:
                result = tryErase( edit._before._placementID, edit._amount );
                break;
            case EditKind::Move:
                result = tryRelocate( edit._after._placementID, edit._after._transform );
                break;
        }
        if ( result == HousingPlaceResult::Ok )
            ++_editCursor;
        return result;
    }

    // ------------------------------------------------------------------------------
    // 상태
    // ------------------------------------------------------------------------------
    bool HousingLayout::rebuildOccupancy()
    {
        std::fill( _listCellOwner.begin(), _listCellOwner.end(), 0u );
        // 1) 부지 · 층 · 겹침을 보며 모두 채운다. 2) 다 채운 뒤 받침을 본다(위층 바닥이 아래 벽보다 앞 id 일 수 있다).
        for ( const HousingPlacement& placement : _listPlacement )
        {
            const HousingPartDef* pPart = _pCatalog->findPart( placement._partID );
            int2                  cellMin;
            int2                  cellMax;
            HousingPlaceResult    result = checkFootprint( *pPart, placement._transform, 0, cellMin, cellMax );
            if ( result == HousingPlaceResult::NoSupport )
                result = HousingPlaceResult::Ok; // 받침은 2) 에서 본다
            if ( result != HousingPlaceResult::Ok )
            {
                SW_LOG_ERROR_ID( "HousingLayout.SavedPlacementBreaksRules", "Housing plot '%#': saved placement %# ('%#') is %# under the current rules - the save is refused", _settings._plotID.c_str(),
                                 placement._placementID, placement._partID.c_str(), getEnumName( result ) );
                return false;
            }
            occupy( placement, placement._placementID );
        }
        for ( const HousingPlacement& placement : _listPlacement )
        {
            const HousingPartDef* pPart = _pCatalog->findPart( placement._partID );
            int2                  cellMin;
            int2                  cellMax;
            computeCells( *pPart, placement._transform, cellMin, cellMax );
            if ( isSupported( *pPart, placement._transform._level, cellMin, cellMax, 0 ) == false )
            {
                SW_LOG_ERROR_ID( "HousingLayout.SavedPlacementBreaksRules", "Housing plot '%#': saved placement %# ('%#') is %# under the current rules - the save is refused", _settings._plotID.c_str(),
                                 placement._placementID, placement._partID.c_str(), getEnumName( HousingPlaceResult::NoSupport ) );
                return false;
            }
        }
        return true;
    }

    void HousingLayout::serializeState( StateIO& io )
    {
        using Internal = HousingLayoutInternal;
        io.header( kStateTag, kStateVersion );
        hashed_string plotID = _settings._plotID;
        io.value( plotID );
        if ( io.isReading() && io.isOk() && plotID != _settings._plotID )
        {
            SW_LOG_ERROR_ID( "HousingLayout.SavedPlotDiffers", "Housing plot '%#': the save belongs to plot '%#' - refused", _settings._plotID.c_str(), plotID.c_str() );
            io.fail();
            return;
        }
        io.value( _nextPlacementID );
        io.list( _listPlacement, Internal::kPlacementMin, [&io]( HousingPlacement& placement )
        { io.value( placement ); } );
        if ( io.isWriting() || io.isOk() == false )
            return;
        if ( _pCatalog == nullptr )
        {
            io.fail(); // initialize 전에는 부품을 찾을 수 없다
            return;
        }
        uint32 previousID = 0;
        for ( HousingPlacement& placement : _listPlacement )
        {
            // id 는 오름차순이고 다음 id 보다 작다 — 겹치거나 앞지른 id 는 불러온 뒤 놓은 것과 부딪힌다.
            if ( io.check( placement._placementID > previousID && placement._placementID < _nextPlacementID ) == false )
            {
                SW_LOG_ERROR_ID( "HousingLayout.SavedPlacementIDInvalid", "Housing plot '%#': saved placement id %# is out of order or not below the next id %#", _settings._plotID.c_str(),
                                 placement._placementID, _nextPlacementID );
                return;
            }
            previousID                  = placement._placementID;
            const HousingPartDef* pPart = _pCatalog->findPart( placement._partID );
            if ( pPart == nullptr )
            {
                // 지운 부품은 이어받은 부품의 지금 판으로 읽는다. 이어받은 것이 없으면 콘텐츠 누락이라 거절한다(조용히 빼면 그 가구가 사라진다).
                pPart = _pCatalog->findReplacement( placement._partID );
                if ( pPart == nullptr )
                {
                    SW_LOG_ERROR_ID( "HousingLayout.SavedPartUnknown", "Housing plot '%#': saved placement %# uses part '%#', which the catalog neither has nor replaces - the save is refused",
                                     _settings._plotID.c_str(), placement._placementID, placement._partID.c_str() );
                    io.fail();
                    return;
                }
                placement._partID      = pPart->_id;
                placement._partVersion = pPart->_version;
                continue;
            }
            if ( placement._partVersion < pPart->_minLoadVersion || placement._partVersion > pPart->_version )
            {
                SW_LOG_ERROR_ID( "HousingLayout.SavedPartVersionUnsupported", "Housing plot '%#': saved placement %# has part '%#' version %#, but the catalog reads versions %#..%# - the save is refused",
                                 _settings._plotID.c_str(), placement._placementID, placement._partID.c_str(), placement._partVersion, pPart->_minLoadVersion, pPart->_version );
                io.fail();
                return;
            }
            placement._partVersion = pPart->_version;
        }
        if ( io.check( rebuildOccupancy() ) == false )
            return;
        _listEdit.clear();
        _editCursor = 0;
        ++_revision;
    }
} // namespace sw
