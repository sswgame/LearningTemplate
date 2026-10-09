#include "pch.h"

#include "GameFramework/Base/World/LandRegistry.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    LandRegistry::LandRegistry()
        : _listCell{}
        , _listOwnerName{}
        , _topology{}
        , _origin{}
        , _cellSize{ 1.0f }
        , _revision{ 0 }
    {
    }

    void LandRegistry::initialize( int32 width, int32 height, float32 cellSize, const float3& origin )
    {
        _topology = GridTopology( width, height );
        _listCell.assign( static_cast<size_t>( _topology.getCellCount() ), Cell{} );
        _listOwnerName.clear();
        _origin   = origin;
        _cellSize = cellSize > 0.0f ? cellSize : 1.0f;
        ++_revision;
    }

    uint16 LandRegistry::registerOwner( const hashed_string& ownerName )
    {
        if ( ownerName.empty() || isInitialized() == false )
            return kNoOwner;
        for ( size_t index = 0; index < _listOwnerName.size(); ++index )
        {
            if ( _listOwnerName[index] == ownerName )
                return static_cast<uint16>( index + 1 );
        }
        _listOwnerName.push_back( ownerName );
        return static_cast<uint16>( _listOwnerName.size() );
    }

    bool LandRegistry::claimRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY, bool bBlocking )
    {
        const bool bValid = owner != kNoOwner && owner <= _listOwnerName.size() && minX <= maxX && minY <= maxY && _topology.isInside( minX, minY ) &&
                            _topology.isInside( maxX, maxY );
        if ( bValid == false )
            return false;
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                if ( isUsableBy( owner, x, y ) == false )
                    return false;
            }
        }
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                Cell& cell      = _listCell[static_cast<size_t>( _topology.toIndex( x, y ) )];
                cell._owner     = owner;
                cell._bBlocking = bBlocking ? SW_TRUE : SW_FALSE;
            }
        }
        ++_revision;
        return true;
    }

    void LandRegistry::releaseRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY )
    {
        if ( owner == kNoOwner )
            return;
        const int32 fromX    = MathUtil::max( 0, minX );
        const int32 fromY    = MathUtil::max( 0, minY );
        const int32 toX      = MathUtil::min( _topology._width - 1, maxX );
        const int32 toY      = MathUtil::min( _topology._height - 1, maxY );
        bool        bChanged = false;
        for ( int32 y = fromY; y <= toY; ++y )
        {
            for ( int32 x = fromX; x <= toX; ++x )
            {
                Cell& cell = _listCell[static_cast<size_t>( _topology.toIndex( x, y ) )];
                if ( cell._owner != owner )
                    continue;
                cell     = Cell{};
                bChanged = true;
            }
        }
        if ( bChanged )
            ++_revision;
    }

    bool LandRegistry::claimWorldRect( uint16 owner, const float3& center, const float3& size, bool bBlocking )
    {
        int2 minCell;
        int2 maxCell;
        computeWorldRect( center, size, minCell, maxCell );
        return claimRect( owner, minCell._x, minCell._y, maxCell._x, maxCell._y, bBlocking );
    }

    void LandRegistry::releaseWorldRect( uint16 owner, const float3& center, const float3& size )
    {
        int2 minCell;
        int2 maxCell;
        computeWorldRect( center, size, minCell, maxCell );
        releaseRect( owner, minCell._x, minCell._y, maxCell._x, maxCell._y );
    }

    uint16 LandRegistry::getOwner( int32 x, int32 y ) const
    {
        return _topology.isInside( x, y ) ? _listCell[static_cast<size_t>( _topology.toIndex( x, y ) )]._owner : kNoOwner;
    }

    hashed_string LandRegistry::getOwnerName( int32 x, int32 y ) const
    {
        const uint16 owner = getOwner( x, y );
        return owner != kNoOwner ? _listOwnerName[static_cast<size_t>( owner - 1 )] : hashed_string{};
    }

    bool LandRegistry::isUsableBy( uint16 owner, int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return false;
        const uint16 cellOwner = _listCell[static_cast<size_t>( _topology.toIndex( x, y ) )]._owner;
        return cellOwner == kNoOwner || cellOwner == owner;
    }

    bool LandRegistry::isBlockedFor( uint16 owner, int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return false;
        const Cell& cell = _listCell[static_cast<size_t>( _topology.toIndex( x, y ) )];
        return cell._owner != kNoOwner && cell._owner != owner && cell._bBlocking == SW_TRUE;
    }

    int2 LandRegistry::computeCell( const float3& worldPosition ) const
    {
        return int2{ static_cast<int32>( MathUtil::floor( ( worldPosition._x - _origin._x ) / _cellSize ) ),
                     static_cast<int32>( MathUtil::floor( ( worldPosition._z - _origin._z ) / _cellSize ) ) };
    }

    void LandRegistry::computeWorldRect( const float3& center, const float3& size, int2& outMin, int2& outMax ) const
    {
        // 가장자리에 딱 닿는 칸은 넣지 않는다 — 2 m 사각은 1 m 칸 둘을 덮는다(셋이 아니다).
        constexpr float32 kEdgeEpsilon = 0.0001f;
        const float32     halfX        = MathUtil::max( 0.0f, size._x * 0.5f - kEdgeEpsilon );
        const float32     halfZ        = MathUtil::max( 0.0f, size._z * 0.5f - kEdgeEpsilon );
        outMin                         = computeCell( float3{ center._x - halfX, center._y, center._z - halfZ } );
        outMax                         = computeCell( float3{ center._x + halfX, center._y, center._z + halfZ } );
    }

    void LandRegistry::writeState( Archive& outArchive ) const
    {
        outArchive << _topology._width;
        outArchive << _topology._height;
        outArchive << static_cast<uint32>( _listOwnerName.size() );
        for ( const hashed_string& ownerName : _listOwnerName )
        {
            StateArchiveUtil::writeName( outArchive, ownerName );
        }
        for ( const Cell& cell : _listCell )
        {
            outArchive << cell._owner;
            outArchive << cell._bBlocking;
        }
    }

    bool LandRegistry::readState( Archive& archive )
    {
        int32  width      = 0;
        int32  height     = 0;
        uint32 ownerCount = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || width != _topology._width || height != _topology._height )
            return false;
        if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), ownerCount ) == false )
            return false;
        // 저장된 번호(1..) → 지금 번호. 이름이 지금 등록에 없으면 새로 등록한다(그 키트가 아직 묶이지 않았어도 칸은 그 이름의 것이다).
        LandRegistry   land = *this;
        vector<uint16> listOwnerMap;
        listOwnerMap.reserve( ownerCount );
        for ( uint32 index = 0; index < ownerCount; ++index )
        {
            hashed_string ownerName;
            if ( StateArchiveUtil::readName( archive, ownerName ) == false || ownerName.empty() )
                return false;
            listOwnerMap.push_back( land.registerOwner( ownerName ) );
        }
        for ( Cell& cell : land._listCell )
        {
            uint16 savedOwner = kNoOwner;
            uint8  bBlocking  = SW_FALSE;
            archive >> savedOwner;
            archive >> bBlocking;
            if ( archive.isError() || savedOwner > ownerCount || bBlocking > SW_TRUE )
                return false;
            cell._owner     = savedOwner == kNoOwner ? kNoOwner : listOwnerMap[static_cast<size_t>( savedOwner - 1 )];
            cell._bBlocking = savedOwner == kNoOwner ? SW_FALSE : bBlocking;
        }
        land._revision = _revision + 1;
        *this          = std::move( land );
        return true;
    }
} // namespace sw
