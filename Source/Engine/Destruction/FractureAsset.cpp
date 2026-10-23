#include "pch.h"

#include "Engine/Destruction/FractureAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "FractureAsset" );

    namespace
    {
        struct FractureAssetInternal
        {
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'F', 'R' };

            /** @brief 리틀 엔디언 쓰기입니다. */
            struct Writer
            {
                vector<uint8>& _bytes;

                void writeUint32( uint32 value )
                {
                    for ( uint32 shift = 0; shift < 32; shift += 8 )
                    {
                        _bytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                    }
                }
                void writeUint64( uint64 value )
                {
                    writeUint32( static_cast<uint32>( value & 0xFFFFFFFFull ) );
                    writeUint32( static_cast<uint32>( value >> 32 ) );
                }
                void writeFloat32( float32 value )
                {
                    uint32 bits = 0;
                    Memory::copy( &bits, &value, sizeof( bits ) );
                    writeUint32( bits );
                }
                void writeFloat3( const float3& value )
                {
                    writeFloat32( value._x );
                    writeFloat32( value._y );
                    writeFloat32( value._z );
                }
            };

            /** @brief 리틀 엔디언 읽기입니다. 끝을 넘으면 `_bFailed` 가 서고 0 을 돌려줍니다. */
            struct Reader
            {
                const uint8* _pData;
                size_t       _size;
                size_t       _offset;
                bool         _bFailed;

                bool hasBytes( size_t count )
                {
                    if ( _bFailed || _size - _offset < count )
                    {
                        _bFailed = true;
                        return false;
                    }
                    return true;
                }
                uint32 readUint32()
                {
                    if ( hasBytes( 4 ) == false )
                        return 0;
                    const uint8* pByte = _pData + _offset;
                    _offset += 4;
                    return static_cast<uint32>( pByte[0] ) | ( static_cast<uint32>( pByte[1] ) << 8 ) | ( static_cast<uint32>( pByte[2] ) << 16 ) |
                           ( static_cast<uint32>( pByte[3] ) << 24 );
                }
                uint64 readUint64()
                {
                    const uint64 low  = readUint32();
                    const uint64 high = readUint32();
                    return low | ( high << 32 );
                }
                float32 readFloat32()
                {
                    const uint32 bits  = readUint32();
                    float32      value = 0.0f;
                    Memory::copy( &value, &bits, sizeof( value ) );
                    return value;
                }
                float3 readFloat3()
                {
                    const float32 x = readFloat32();
                    const float32 y = readFloat32();
                    const float32 z = readFloat32();
                    return float3{ x, y, z };
                }
                uint8 readUint8()
                {
                    if ( hasBytes( 1 ) == false )
                        return 0;
                    return _pData[_offset++];
                }
            };

            /**
             * @brief 파일에 실린 정점 하나의 바이트 수입니다(위치 3 · 노멀 3 · UV 2 · 색 4 의 float32). `sizeof( RHIVertex )` 가 아니다.
             * @details 탄젠트는 싣지 않는다 — 조각 메시는 `Mesh::setVertices` 가 위치 · 노멀 · UV 로 구한다(`FractureRenderUtil`).
             */
            static constexpr uint64 kStoredVertexBytes = 12 * sizeof( float32 );

            /** @brief 원소 수가 남은 바이트로 담길 수 있는지 확인합니다. 잘린 파일이 큰 할당을 일으키지 않게 하려는 것입니다. */
            static bool canHold( const Reader& reader, uint64 count, uint64 elementSize ) { return count * elementSize <= reader._size - reader._offset; }

            /** @brief 노드 · 자식 번호 · 연결을 읽습니다. */
            static void readGraph( Reader& reader, uint32 leafCount, uint32 nodeCount, uint32 childCount, uint32 linkCount, FractureGraph& outGraph )
            {
                outGraph._leafCount = leafCount;
                outGraph._listNode.resize( nodeCount );
                for ( FractureNode& node : outGraph._listNode )
                {
                    node._centroid   = reader.readFloat3();
                    node._volume     = reader.readFloat32();
                    node._parent     = static_cast<int32>( reader.readUint32() );
                    node._firstChild = reader.readUint32();
                    node._childCount = reader.readUint32();
                    node._firstLeaf  = reader.readUint32();
                    node._leafCount  = reader.readUint32();
                    node._depth      = static_cast<uint8>( reader.readUint32() );
                }
                outGraph._listChildNode.resize( childCount );
                for ( uint32& child : outGraph._listChildNode )
                {
                    child = reader.readUint32();
                }
                outGraph._listLink.resize( linkCount );
                for ( FractureLink& link : outGraph._listLink )
                {
                    link._leafA = reader.readUint32();
                    link._leafB = reader.readUint32();
                    link._area  = reader.readFloat32();
                }
            }

            /** @brief 조각마다 경계와 정점 · 껍질 점 구간을 읽습니다. */
            static void readPieces( Reader& reader, uint32 leafCount, vector<FracturePiece>& outListPiece )
            {
                outListPiece.resize( leafCount );
                for ( FracturePiece& piece : outListPiece )
                {
                    piece._boundsMin      = reader.readFloat3();
                    piece._boundsMax      = reader.readFloat3();
                    piece._firstVertex    = reader.readUint32();
                    piece._vertexCount    = reader.readUint32();
                    piece._firstHullPoint = reader.readUint32();
                    piece._hullPointCount = reader.readUint32();
                }
            }

            /** @brief 정점 · 삼각형 슬롯 · 껍질 점을 읽습니다. */
            static void readMesh( Reader& reader, uint32 vertexCount, uint32 hullCount, FractureAsset& inoutAsset )
            {
                inoutAsset._listVertex.resize( vertexCount );
                for ( RHIVertex& vertex : inoutAsset._listVertex )
                {
                    for ( float32& value : vertex._arrPosition )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrNormal )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrUv )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrColor )
                    {
                        value = reader.readFloat32();
                    }
                }
                inoutAsset._listTriangleSlot.resize( vertexCount / 3 );
                for ( uint8& slot : inoutAsset._listTriangleSlot )
                {
                    slot = reader.readUint8();
                }
                inoutAsset._listHullPoint.resize( hullCount );
                for ( float3& point : inoutAsset._listHullPoint )
                {
                    point = reader.readFloat3();
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    vector_reference<const RHIVertex> FractureAsset::getPieceVertices( uint32 piece ) const
    {
        if ( piece >= _listPiece.size() )
            return {};
        return vector_reference<const RHIVertex>{ _listVertex.data() + _listPiece[piece]._firstVertex, _listPiece[piece]._vertexCount };
    }

    vector_reference<const float3> FractureAsset::getPieceHull( uint32 piece ) const
    {
        if ( piece >= _listPiece.size() )
            return {};
        return vector_reference<const float3>{ _listHullPoint.data() + _listPiece[piece]._firstHullPoint, _listPiece[piece]._hullPointCount };
    }

    uint32 FractureAsset::countTriangles( FractureSurfaceSlot slot ) const
    {
        uint32 count = 0;
        for ( const uint8 value : _listTriangleSlot )
        {
            if ( value == static_cast<uint8>( slot ) )
                ++count;
        }
        return count;
    }

    bool FractureAsset::isValid( string* pOutError ) const
    {
        if ( _graph.isValid( pOutError ) == false )
            return false;
        if ( _listPiece.size() != _graph._leafCount )
            return FractureGraphUtil::reportInvalid( pOutError, "piece count differs from leaf count" );
        if ( _listVertex.size() % 3 != 0 || _listTriangleSlot.size() * 3 != _listVertex.size() )
            return FractureGraphUtil::reportInvalid( pOutError, "triangle slots do not match the vertex list" );
        uint32 expectedVertex = 0;
        for ( const FracturePiece& piece : _listPiece )
        {
            const bool bInvalidPieceVertexRange = piece._firstVertex != expectedVertex || piece._vertexCount % 3 != 0 || piece._vertexCount == 0;
            if ( bInvalidPieceVertexRange )
                return FractureGraphUtil::reportInvalid( pOutError, "piece vertex ranges must be contiguous triangle lists" );
            expectedVertex += piece._vertexCount;
            if ( static_cast<size_t>( piece._firstHullPoint ) + piece._hullPointCount > _listHullPoint.size() || piece._hullPointCount < 3 )
                return FractureGraphUtil::reportInvalid( pOutError, "piece hull out of range or too small" );
        }
        if ( expectedVertex != _listVertex.size() )
            return FractureGraphUtil::reportInvalid( pOutError, "vertex list longer than the pieces" );
        for ( const uint8 slot : _listTriangleSlot )
        {
            if ( slot >= static_cast<uint8>( FractureSurfaceSlot::Count ) )
                return FractureGraphUtil::reportInvalid( pOutError, "unknown surface slot" );
        }
        return true;
    }

    void FractureAsset::clear()
    {
        _graph.clear();
        _listPiece.clear();
        _listVertex.clear();
        _listTriangleSlot.clear();
        _listHullPoint.clear();
        _boundsMin = float3{};
        _boundsMax = float3{};
        _seed      = 0;
    }

    void FractureAsset::makeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        outBytes.reserve( 64 + _listVertex.size() * FractureAssetInternal::kStoredVertexBytes + _graph._listNode.size() * 36 );
        FractureAssetInternal::Writer writer{ outBytes };
        for ( const uint8 magic : FractureAssetInternal::kArrMagic )
        {
            outBytes.push_back( magic );
        }
        writer.writeUint32( kVersion );
        writer.writeUint32( _graph._leafCount );
        writer.writeUint32( static_cast<uint32>( _graph._listNode.size() ) );
        writer.writeUint32( static_cast<uint32>( _graph._listChildNode.size() ) );
        writer.writeUint32( static_cast<uint32>( _graph._listLink.size() ) );
        writer.writeUint32( static_cast<uint32>( _listVertex.size() ) );
        writer.writeUint32( static_cast<uint32>( _listHullPoint.size() ) );
        writer.writeUint64( _seed );
        writer.writeFloat3( _boundsMin );
        writer.writeFloat3( _boundsMax );
        for ( const FractureNode& node : _graph._listNode )
        {
            writer.writeFloat3( node._centroid );
            writer.writeFloat32( node._volume );
            writer.writeUint32( static_cast<uint32>( node._parent ) );
            writer.writeUint32( node._firstChild );
            writer.writeUint32( node._childCount );
            writer.writeUint32( node._firstLeaf );
            writer.writeUint32( node._leafCount );
            writer.writeUint32( node._depth );
        }
        for ( const uint32 child : _graph._listChildNode )
        {
            writer.writeUint32( child );
        }
        for ( const FractureLink& link : _graph._listLink )
        {
            writer.writeUint32( link._leafA );
            writer.writeUint32( link._leafB );
            writer.writeFloat32( link._area );
        }
        for ( const FracturePiece& piece : _listPiece )
        {
            writer.writeFloat3( piece._boundsMin );
            writer.writeFloat3( piece._boundsMax );
            writer.writeUint32( piece._firstVertex );
            writer.writeUint32( piece._vertexCount );
            writer.writeUint32( piece._firstHullPoint );
            writer.writeUint32( piece._hullPointCount );
        }
        for ( const RHIVertex& vertex : _listVertex )
        {
            for ( const float32 value : vertex._arrPosition )
            {
                writer.writeFloat32( value );
            }
            for ( const float32 value : vertex._arrNormal )
            {
                writer.writeFloat32( value );
            }
            for ( const float32 value : vertex._arrUv )
            {
                writer.writeFloat32( value );
            }
            for ( const float32 value : vertex._arrColor )
            {
                writer.writeFloat32( value );
            }
        }
        for ( const uint8 slot : _listTriangleSlot )
        {
            outBytes.push_back( slot );
        }
        for ( const float3& point : _listHullPoint )
        {
            writer.writeFloat3( point );
        }
    }

    bool FractureAsset::readFromBytes( const uint8* pData, size_t size )
    {
        clear();
        const bool bMissingFractureMagic = pData == nullptr || size < 8 || Memory::compare( pData, FractureAssetInternal::kArrMagic, 4 ) != 0;
        if ( bMissingFractureMagic )
        {
            SW_LOG_ERROR( "Not a fracture asset (bad magic)" );
            return false;
        }
        FractureAssetInternal::Reader reader{ pData, size, 4, false };
        const uint32                  version = reader.readUint32();
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "Fracture asset version %# is not %# - re-import the model", version, kVersion );
            return false;
        }
        const uint32 leafCount   = reader.readUint32();
        const uint32 nodeCount   = reader.readUint32();
        const uint32 childCount  = reader.readUint32();
        const uint32 linkCount   = reader.readUint32();
        const uint32 vertexCount = reader.readUint32();
        const uint32 hullCount   = reader.readUint32();
        _seed                    = reader.readUint64();
        _boundsMin               = reader.readFloat3();
        _boundsMax               = reader.readFloat3();
        const bool bFits         = reader._bFailed == false && FractureAssetInternal::canHold( reader, nodeCount, 36 ) && FractureAssetInternal::canHold( reader, childCount, 4 ) &&
                           FractureAssetInternal::canHold( reader, linkCount, 12 ) && FractureAssetInternal::canHold( reader, leafCount, 40 ) &&
                           FractureAssetInternal::canHold( reader, vertexCount, FractureAssetInternal::kStoredVertexBytes ) && FractureAssetInternal::canHold( reader, hullCount, 12 );
        if ( bFits == false )
        {
            SW_LOG_ERROR( "Fracture asset is truncated" );
            clear();
            return false;
        }
        FractureAssetInternal::readGraph( reader, leafCount, nodeCount, childCount, linkCount, _graph );
        FractureAssetInternal::readPieces( reader, leafCount, _listPiece );
        FractureAssetInternal::readMesh( reader, vertexCount, hullCount, *this );

        string     error;
        const bool bMalformedFractureAsset = reader._bFailed || reader._offset != size || isValid( &error ) == false;
        if ( bMalformedFractureAsset )
        {
            SW_LOG_ERROR( "Fracture asset is malformed: %#", reader._bFailed || reader._offset != size ? "length mismatch" : error.c_str() );
            clear();
            return false;
        }
        return true;
    }

    bool FractureAsset::saveToFile( string_view path ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool FractureAsset::loadFromResource( string_view path )
    {
        clear();
        vector<uint8> bytes;
        if ( ResourceIO::readBinaryResource( path, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size() );
    }

    string FractureAsset::makePathForMesh( string_view meshPath )
    {
        const size_t dot = meshPath.rfind( '.' );
        string       path{ dot == string_view::npos ? meshPath : meshPath.substr( 0, dot ) };
        path += kExtension;
        return path;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( FractureAsset, FractureAsset::kAssetType );
} // namespace sw
