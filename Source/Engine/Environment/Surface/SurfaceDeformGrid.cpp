#include "pch.h"

#include "Engine/Environment/Surface/SurfaceDeformGrid.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Surface/SurfaceStamp.h"

namespace sw
{
    void SurfaceDeformGrid::TexelRect::merge( const TexelRect& other )
    {
        if ( other.isEmpty() )
            return;
        if ( isEmpty() )
        {
            *this = other;
            return;
        }
        _minX = MathUtil::min( _minX, other._minX );
        _minZ = MathUtil::min( _minZ, other._minZ );
        _maxX = MathUtil::max( _maxX, other._maxX );
        _maxZ = MathUtil::max( _maxZ, other._maxZ );
    }

    SurfaceDeformGrid::SurfaceDeformGrid()
        : _listHeight{}
        , _listFillSpeed{}
        , _resolution{ 0 }
        , _extent{ 0.0f }
        , _texelSize{ 0.0f }
        , _originCellX{ 0 }
        , _originCellZ{ 0 }
        , _activeRect{}
        , _dirtyRect{}
    {
    }

    void SurfaceDeformGrid::reset( uint32 resolution, float32 extent, float32 centerX, float32 centerZ )
    {
        _resolution = resolution;
        _extent     = extent;
        _texelSize  = extent / static_cast<float32>( resolution );
        _listHeight.assign( static_cast<size_t>( resolution ) * resolution, 0.0f );
        _listFillSpeed.assign( static_cast<size_t>( resolution ) * resolution, 0.0f );
        const int32 half = static_cast<int32>( resolution / 2 );
        _originCellX     = toWorldCell( centerX ) - half;
        _originCellZ     = toWorldCell( centerZ ) - half;
        _activeRect      = TexelRect{};
        _dirtyRect       = TexelRect{ 0, 0, resolution, resolution };
    }

    bool SurfaceDeformGrid::recenter( float32 centerX, float32 centerZ )
    {
        const int32 half    = static_cast<int32>( _resolution / 2 );
        const int32 shiftX  = toWorldCell( centerX ) - half - _originCellX;
        const int32 shiftZ  = toWorldCell( centerZ ) - half - _originCellZ;
        const int32 extentN = static_cast<int32>( _resolution );
        if ( shiftX == 0 && shiftZ == 0 )
            return false;
        _originCellX += shiftX;
        _originCellZ += shiftZ;
        const bool bOverlaps = MathUtil::abs( shiftX ) < extentN && MathUtil::abs( shiftZ ) < extentN;
        if ( bOverlaps == false )
        {
            _listHeight.assign( _listHeight.size(), 0.0f );
            _listFillSpeed.assign( _listFillSpeed.size(), 0.0f );
            _activeRect = TexelRect{};
            _dirtyRect  = TexelRect{ 0, 0, _resolution, _resolution };
            return true;
        }
        // 새 칸 (x, z) 는 옛 칸 (x + shiftX, z + shiftZ) 다 — 사본에서 읽어 겹치는 칸만 옮긴다(움직임은 드물다 — 창 너비의 일부만큼 벗어날 때).
        const vector<float32> listOldHeight    = _listHeight;
        const vector<float32> listOldFillSpeed = _listFillSpeed;
        _listHeight.assign( listOldHeight.size(), 0.0f );
        _listFillSpeed.assign( listOldFillSpeed.size(), 0.0f );
        TexelRect moved;
        for ( int32 z = 0; z < extentN; ++z )
        {
            const int32 oldZ = z + shiftZ;
            if ( oldZ < 0 || oldZ >= extentN )
                continue;
            for ( int32 x = 0; x < extentN; ++x )
            {
                const int32 oldX = x + shiftX;
                if ( oldX < 0 || oldX >= extentN )
                    continue;
                const uint32 oldIndex = toIndex( static_cast<uint32>( oldX ), static_cast<uint32>( oldZ ) );
                if ( listOldHeight[oldIndex] == 0.0f )
                    continue;
                const uint32 newIndex    = toIndex( static_cast<uint32>( x ), static_cast<uint32>( z ) );
                _listHeight[newIndex]    = listOldHeight[oldIndex];
                _listFillSpeed[newIndex] = listOldFillSpeed[oldIndex];
                moved.merge( TexelRect{ static_cast<uint32>( x ), static_cast<uint32>( z ), static_cast<uint32>( x ) + 1, static_cast<uint32>( z ) + 1 } );
            }
        }
        _activeRect = moved;
        _dirtyRect  = TexelRect{ 0, 0, _resolution, _resolution };
        return true;
    }

    void SurfaceDeformGrid::applyStamp( const SurfaceStampDef& def, float32 centerX, float32 centerZ, float32 yaw, float32 length, float32 phase )
    {
        if ( isReady() == false || def.isSet() == false )
            return;
        const float32 reach  = def.computeInfluenceRadius( length );
        const int32   extent = static_cast<int32>( _resolution );
        const int32   minX   = MathUtil::max( toWorldCell( centerX - reach ) - _originCellX, 0 );
        const int32   minZ   = MathUtil::max( toWorldCell( centerZ - reach ) - _originCellZ, 0 );
        const int32   maxX   = MathUtil::min( toWorldCell( centerX + reach ) - _originCellX + 1, extent );
        const int32   maxZ   = MathUtil::min( toWorldCell( centerZ + reach ) - _originCellZ + 1, extent );
        if ( minX >= maxX || minZ >= maxZ )
            return;
        // 요 규칙(OrientationUtil): 앞 = (sin, 0, cos), 오른쪽 = (cos, 0, -sin).
        const float32 sinYaw  = MathUtil::sin( yaw );
        const float32 cosYaw  = MathUtil::cos( yaw );
        const float2  origin  = getOrigin();
        bool          bChange = false;
        for ( int32 z = minZ; z < maxZ; ++z )
        {
            const float32 offsetZ = origin._y + ( static_cast<float32>( z ) + 0.5f ) * _texelSize - centerZ;
            for ( int32 x = minX; x < maxX; ++x )
            {
                const float32 offsetX = origin._x + ( static_cast<float32>( x ) + 0.5f ) * _texelSize - centerX;
                const float32 right   = offsetX * cosYaw - offsetZ * sinYaw;
                const float32 forward = offsetX * sinYaw + offsetZ * cosYaw;
                const float32 height  = def.computeHeight( right, forward, length, phase );
                const uint32  index   = toIndex( static_cast<uint32>( x ), static_cast<uint32>( z ) );
                float32&      cell    = _listHeight[index];
                // 꺼짐은 더 깊은 쪽, 솟음은 꺼지지 않은 칸에서만 더 높은 쪽 — 다른 자국의 테두리가 자국을 메우지 않는다.
                const bool bSinks  = height < 0.0f && height < cell;
                const bool bRaises = height > 0.0f && cell >= 0.0f && height > cell;
                if ( bSinks == false && bRaises == false )
                    continue;
                cell                  = height;
                _listFillSpeed[index] = def._fillSpeed;
                bChange               = true;
            }
        }
        if ( bChange == false )
            return;
        const TexelRect stamped{ static_cast<uint32>( minX ), static_cast<uint32>( minZ ), static_cast<uint32>( maxX ), static_cast<uint32>( maxZ ) };
        _activeRect.merge( stamped );
        _dirtyRect.merge( stamped );
    }

    void SurfaceDeformGrid::fill( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _activeRect.isEmpty() )
            return;
        // 메운 칸은 지난번 모습과 다르다 — 옛 사각형 전부를 보내고, 아직 남은 칸으로 사각형을 줄인다.
        _dirtyRect.merge( _activeRect );
        TexelRect remaining;
        for ( uint32 z = _activeRect._minZ; z < _activeRect._maxZ; ++z )
        {
            for ( uint32 x = _activeRect._minX; x < _activeRect._maxX; ++x )
            {
                const uint32 index = toIndex( x, z );
                float32&     cell  = _listHeight[index];
                if ( cell == 0.0f )
                    continue;
                const float32 step = _listFillSpeed[index] * deltaTime;
                cell               = cell < 0.0f ? MathUtil::min( cell + step, 0.0f ) : MathUtil::max( cell - step, 0.0f );
                if ( cell != 0.0f )
                    remaining.merge( TexelRect{ x, z, x + 1, z + 1 } );
            }
        }
        _activeRect = remaining;
    }

    float32 SurfaceDeformGrid::findHeightAt( float32 x, float32 z ) const
    {
        if ( isReady() == false )
            return 0.0f;
        const int32 cellX = toWorldCell( x ) - _originCellX;
        const int32 cellZ = toWorldCell( z ) - _originCellZ;
        const int32 limit = static_cast<int32>( _resolution );
        if ( cellX < 0 || cellZ < 0 || cellX >= limit || cellZ >= limit )
            return 0.0f;
        return _listHeight[toIndex( static_cast<uint32>( cellX ), static_cast<uint32>( cellZ ) )];
    }

    uint32 SurfaceDeformGrid::countActiveTexels() const
    {
        uint32 activeCount = 0;
        for ( uint32 z = _activeRect._minZ; z < _activeRect._maxZ; ++z )
        {
            for ( uint32 x = _activeRect._minX; x < _activeRect._maxX; ++x )
            {
                activeCount += _listHeight[toIndex( x, z )] != 0.0f ? 1u : 0u;
            }
        }
        return activeCount;
    }

    float32 SurfaceDeformGrid::computeMaxDepth() const
    {
        float32 deepest = 0.0f;
        for ( uint32 z = _activeRect._minZ; z < _activeRect._maxZ; ++z )
        {
            for ( uint32 x = _activeRect._minX; x < _activeRect._maxX; ++x )
            {
                deepest = MathUtil::max( deepest, -_listHeight[toIndex( x, z )] );
            }
        }
        return deepest;
    }

    float32 SurfaceDeformGrid::computeMaxDepthNear( float32 centerX, float32 centerZ, float32 halfSize ) const
    {
        if ( isReady() == false || _activeRect.isEmpty() )
            return 0.0f;
        // 자국이 남은 사각형과 겹치는 칸만 본다.
        const int32 minX    = MathUtil::max( toWorldCell( centerX - halfSize ) - _originCellX, static_cast<int32>( _activeRect._minX ) );
        const int32 minZ    = MathUtil::max( toWorldCell( centerZ - halfSize ) - _originCellZ, static_cast<int32>( _activeRect._minZ ) );
        const int32 maxX    = MathUtil::min( toWorldCell( centerX + halfSize ) - _originCellX + 1, static_cast<int32>( _activeRect._maxX ) );
        const int32 maxZ    = MathUtil::min( toWorldCell( centerZ + halfSize ) - _originCellZ + 1, static_cast<int32>( _activeRect._maxZ ) );
        float32     deepest = 0.0f;
        for ( int32 z = minZ; z < maxZ; ++z )
        {
            for ( int32 x = minX; x < maxX; ++x )
            {
                deepest = MathUtil::max( deepest, -_listHeight[toIndex( static_cast<uint32>( x ), static_cast<uint32>( z ) )] );
            }
        }
        return deepest;
    }

    void SurfaceDeformGrid::writeTexels( const TexelRect& rect, float32 depthRange, vector<uint8>& outListByte ) const
    {
        outListByte.assign( static_cast<size_t>( rect.getWidth() ) * rect.getHeight() * 4, 255 );
        const float32 scale = depthRange > 0.0f ? 255.0f / depthRange : 0.0f;
        size_t        byte  = 0;
        for ( uint32 z = rect._minZ; z < rect._maxZ; ++z )
        {
            for ( uint32 x = rect._minX; x < rect._maxX; ++x )
            {
                const float32 height  = _listHeight[toIndex( x, z )];
                const float32 sink    = MathUtil::clamp( -height * scale, 0.0f, 255.0f );
                const float32 raise   = MathUtil::clamp( height * scale, 0.0f, 255.0f );
                outListByte[byte]     = static_cast<uint8>( MathUtil::round( 255.0f - sink ) );
                outListByte[byte + 1] = static_cast<uint8>( MathUtil::round( 255.0f - raise ) );
                byte += 4;
            }
        }
    }

    float2 SurfaceDeformGrid::getOrigin() const
    {
        return float2{ static_cast<float32>( _originCellX ) * _texelSize, static_cast<float32>( _originCellZ ) * _texelSize };
    }

    int32 SurfaceDeformGrid::toWorldCell( float32 world ) const
    {
        return static_cast<int32>( MathUtil::floor( world / _texelSize ) );
    }
} // namespace sw
