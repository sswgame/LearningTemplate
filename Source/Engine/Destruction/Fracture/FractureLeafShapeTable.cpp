#include "pch.h"

#include "Engine/Destruction/Fracture/FractureLeafShapeTable.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    void FractureLeafHullUtil::makeLeafHull( const FractureAsset& asset, uint32 leaf, float32 scale, float32 shrink, vector<float3>& outListPoint )
    {
        outListPoint.clear();
        const float3& centroid = asset._graph._listNode[leaf]._centroid;
        for ( const float3& offset : asset.getPieceHull( leaf ) )
        {
            const float32 length = offset.getLength();
            const float32 keep   = length > 0.0f ? MathUtil::max( 0.0f, 1.0f - shrink / ( length * scale ) ) : 0.0f;
            outListPoint.push_back( ( centroid + offset * keep ) * scale );
        }
    }

    void FractureLeafHullUtil::makeConvexPolygon( const vector<float3>& listPoint, uint32 maxPoint, vector<float2>& outListPolygon )
    {
        vector<float2> listSorted;
        for ( const float3& point : listPoint )
        {
            listSorted.push_back( float2{ point._x, point._y } );
        }
        VectorUtil::sort( listSorted, []( const float2& lhs, const float2& rhs )
        { return lhs._x < rhs._x || ( lhs._x == rhs._x && lhs._y < rhs._y ); } );
        outListPolygon.clear();
        const auto cross = []( const float2& origin, const float2& a, const float2& b )
        { return ( a._x - origin._x ) * ( b._y - origin._y ) - ( a._y - origin._y ) * ( b._x - origin._x ); };
        for ( const float2& point : listSorted )
        {
            while ( outListPolygon.size() >= 2 && cross( outListPolygon[outListPolygon.size() - 2], outListPolygon.back(), point ) <= 0.0f )
            {
                outListPolygon.pop_back();
            }
            outListPolygon.push_back( point );
        }
        const size_t lowerCount = outListPolygon.size();
        for ( size_t index = listSorted.size(); index > 0; --index )
        {
            const float2& point = listSorted[index - 1];
            while ( outListPolygon.size() > lowerCount && cross( outListPolygon[outListPolygon.size() - 2], outListPolygon.back(), point ) <= 0.0f )
            {
                outListPolygon.pop_back();
            }
            outListPolygon.push_back( point );
        }
        if ( outListPolygon.size() > 1 )
            outListPolygon.pop_back();
        while ( outListPolygon.size() > maxPoint )
        {
            size_t  weakest  = 0;
            float32 smallest = MathUtil::kMaxFloat;
            for ( size_t index = 0; index < outListPolygon.size(); ++index )
            {
                const size_t  count = outListPolygon.size();
                const float32 area  = MathUtil::abs( cross( outListPolygon[( index + count - 1 ) % count], outListPolygon[index], outListPolygon[( index + 1 ) % count] ) );
                if ( area < smallest )
                {
                    smallest = area;
                    weakest  = index;
                }
            }
            outListPolygon.erase( outListPolygon.begin() + static_cast<std::ptrdiff_t>( weakest ) );
        }
    }
} // namespace sw

namespace sw
{
    FractureLeafShapeSet::FractureLeafShapeSet( IPhysicsScene3D* pScene3D, IPhysicsScene2D* pScene2D, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink,
                                                const hashed_string& material )
        : _pScene3D{ pScene3D }
        , _pScene2D{ pScene2D }
        , _asset{ std::move( asset ) }
        , _listShape{}
        , _material{ material }
        , _scale{ scale }
        , _shrink{ shrink }
        , _pendingCount{ 0 }
        , _nextWarmLeaf{ 0 }
    {
        _pendingCount = _asset->getPieceCount();
        _listShape.assign( _pendingCount, PhysicsShapeHandle{} );
    }

    FractureLeafShapeSet::FractureLeafShapeSet( IPhysicsScene3D& scene, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink, const hashed_string& material )
        : FractureLeafShapeSet( &scene, nullptr, std::move( asset ), scale, shrink, material )
    {
    }

    FractureLeafShapeSet::FractureLeafShapeSet( IPhysicsScene2D& scene, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink, const hashed_string& material )
        : FractureLeafShapeSet( nullptr, &scene, std::move( asset ), scale, shrink, material )
    {
    }

    FractureLeafShapeSet::~FractureLeafShapeSet()
    {
        for ( const PhysicsShapeHandle& shape : _listShape )
        {
            if ( shape.isValid() == false )
                continue;
            if ( _pScene3D != nullptr )
                _pScene3D->destroyShape( shape );
            else
                _pScene2D->destroyShape( shape );
        }
    }

    PhysicsShapeHandle FractureLeafShapeSet::getOrCreateShape( uint32 leaf )
    {
        PhysicsShapeHandle& slot = _listShape[leaf];
        if ( slot.isValid() )
            return slot;
        vector<float3> listPoint;
        FractureLeafHullUtil::makeLeafHull( *_asset, leaf, _scale, _shrink, listPoint );
        if ( _pScene3D != nullptr )
        {
            PhysicsShapeDesc3D shape;
            shape._type      = PhysicsShapeType3D::ConvexHull;
            shape._listPoint = std::move( listPoint );
            slot             = _pScene3D->createShape( vector_reference<const PhysicsShapeDesc3D>{ &shape, 1 }, _material );
        }
        else
        {
            PhysicsShapeDesc2D shape;
            shape._type = PhysicsShapeType2D::Polygon;
            FractureLeafHullUtil::makeConvexPolygon( listPoint, FractureLeafHullUtil::kMaxPolygonPoint2D, shape._listPoint );
            slot = _pScene2D->createShape( vector_reference<const PhysicsShapeDesc2D>{ &shape, 1 }, _material );
        }
        if ( slot.isValid() )
            --_pendingCount;
        return slot;
    }

    uint32 FractureLeafShapeSet::warm( int64 deadlineNanoseconds )
    {
        const uint32 pendingBefore = _pendingCount;
        const uint32 leafCount     = static_cast<uint32>( _listShape.size() );
        while ( _pendingCount > 0 && _nextWarmLeaf < leafCount )
        {
            const uint32 leaf = _nextWarmLeaf++;
            if ( _listShape[leaf].isValid() )
                continue;
            (void)getOrCreateShape( leaf ); // 모양을 있게만 한다 — 쓰는 쪽이 다시 찾는다
            const bool bWarmUpDeadlineReached = deadlineNanoseconds != 0 && MonotonicClock::nowNanoseconds() >= deadlineNanoseconds;
            if ( bWarmUpDeadlineReached )
                break;
        }
        return pendingBefore - _pendingCount;
    }
} // namespace sw

namespace sw
{
    SW_SCENE_SUBSYSTEM( FractureLeafShapeTable, FlushEnd, SceneFrameAccess{} )

    FractureLeafShapeTable::FractureLeafShapeTable( GameObjectManager& manager )
        : _table{}
    {
        (void)manager;
    }

    size_t FractureLeafShapeTable::KeyHash::operator()( const Key& key ) const
    {
        uint64 hash = HashUtil::combine( reinterpret_cast<uintptr_t>( key._pScene ), reinterpret_cast<uintptr_t>( key._pAsset ) );
        hash        = HashUtil::combine( hash, static_cast<uint64>( key._material.getHash() ) );
        hash        = HashUtil::combine( hash, static_cast<uint64>( BitUtil::floatToBits( key._scale ) ) );
        hash        = HashUtil::combine( hash, static_cast<uint64>( BitUtil::floatToBits( key._shrink ) ) );
        return static_cast<size_t>( hash );
    }

    shared_ptr<FractureLeafShapeSet> FractureLeafShapeTable::acquire( IPhysicsScene3D& scene, const shared_ptr<const FractureAsset>& asset, float32 scale, float32 shrink,
                                                                      const hashed_string& material )
    {
        return _table.acquire( Key{ &scene, asset.get(), material, scale, shrink }, [&]( const Key& )
        { return sw::make_shared<FractureLeafShapeSet>( scene, asset, scale, shrink, material ); } );
    }

    shared_ptr<FractureLeafShapeSet> FractureLeafShapeTable::acquire( IPhysicsScene2D& scene, const shared_ptr<const FractureAsset>& asset, float32 scale, float32 shrink,
                                                                      const hashed_string& material )
    {
        return _table.acquire( Key{ &scene, asset.get(), material, scale, shrink }, [&]( const Key& )
        { return sw::make_shared<FractureLeafShapeSet>( scene, asset, scale, shrink, material ); } );
    }
} // namespace sw
