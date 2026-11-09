#include "pch.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

namespace sw
{
    namespace
    {
        struct FractureAssetInternal
        {
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'F', 'R' };

            static void writeFloat3( ByteWriter& writer, const float3& value )
            {
                writer.writeFloat32( value._x );
                writer.writeFloat32( value._y );
                writer.writeFloat32( value._z );
            }

            static float3 readFloat3( ByteReader& reader )
            {
                const float32 x = reader.readFloat32();
                const float32 y = reader.readFloat32();
                const float32 z = reader.readFloat32();
                return float3{ x, y, z };
            }

            /**
             * @brief 파일에 실린 정점 하나의 바이트 수입니다(위치 3 · 노멀 3 · UV 2 · 색 4 의 float32). `sizeof( RHIVertex )` 가 아니다.
             * @details 탄젠트는 싣지 않는다 — 조각 메시는 `Mesh::setVertices` 가 위치 · 노멀 · UV 로 구한다(`FractureRenderUtil`).
             */
            static constexpr uint64 kStoredVertexBytes = 12 * sizeof( float32 );

            static constexpr size_t kStoredNodeBytes  = 10 * sizeof( float32 ); ///< 중심 3 · 부피 · 부모 · 첫 자식 · 자식 수 · 첫 잎 · 잎 수 · 깊이
            static constexpr size_t kStoredLinkBytes  = 3 * sizeof( float32 );  ///< 잎 둘 · 면적
            static constexpr size_t kStoredPieceBytes = 10 * sizeof( float32 ); ///< 경계 6 · 정점 구간 2 · 껍질 구간 2

            /** @brief 노드 · 자식 번호 · 연결을 읽습니다. */
            [[nodiscard]] static bool readGraph( ByteReader& reader, uint32 leafCount, uint32 nodeCount, uint32 childCount, uint32 linkCount, FractureGraph& outGraph )
            {
                outGraph._leafCount = leafCount;
                const bool bNodes   = reader.readList( outGraph._listNode, nodeCount, kStoredNodeBytes, [&]( FractureNode& node )
                  {
                    node._centroid   = readFloat3( reader );
                    node._volume     = reader.readFloat32();
                    node._parent     = static_cast<int32>( reader.readUint32() );
                    node._firstChild = reader.readUint32();
                    node._childCount = reader.readUint32();
                    node._firstLeaf  = reader.readUint32();
                    node._leafCount  = reader.readUint32();
                    node._depth      = static_cast<uint8>( reader.readUint32() );
                } );
                return bNodes && reader.readPodList( outGraph._listChildNode, childCount ) && reader.readList( outGraph._listLink, linkCount, kStoredLinkBytes, [&]( FractureLink& link )
                {
                    link._leafA = reader.readUint32();
                    link._leafB = reader.readUint32();
                    link._area  = reader.readFloat32();
                } );
            }

            /** @brief 조각마다 경계와 정점 · 껍질 점 구간을 읽습니다. */
            [[nodiscard]] static bool readPieces( ByteReader& reader, uint32 leafCount, vector<FracturePiece>& outListPiece )
            {
                return reader.readList( outListPiece, leafCount, kStoredPieceBytes, [&]( FracturePiece& piece )
                {
                    piece._boundsMin      = readFloat3( reader );
                    piece._boundsMax      = readFloat3( reader );
                    piece._firstVertex    = reader.readUint32();
                    piece._vertexCount    = reader.readUint32();
                    piece._firstHullPoint = reader.readUint32();
                    piece._hullPointCount = reader.readUint32();
                } );
            }

            /** @brief 정점 · 삼각형 슬롯 · 껍질 점을 읽습니다. */
            [[nodiscard]] static bool readMesh( ByteReader& reader, uint32 vertexCount, uint32 hullCount, FractureAsset& inoutAsset )
            {
                const bool bVertices = reader.readList( inoutAsset._listVertex, vertexCount, kStoredVertexBytes, [&]( RHIVertex& vertex )
                {
                    for ( float32& value : vertex._arrPosition )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrNormal )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrUV )
                    {
                        value = reader.readFloat32();
                    }
                    for ( float32& value : vertex._arrColor )
                    {
                        value = reader.readFloat32();
                    }
                } );
                return bVertices && reader.readPodList( inoutAsset._listTriangleSlot, vertexCount / 3 ) && reader.readList( inoutAsset._listHullPoint, hullCount, 3 * sizeof( float32 ), [&]( float3& point )
                {
                    point = readFloat3( reader );
                } );
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

    expected<void> FractureAsset::validate() const
    {
        if ( expected<void> graph = _graph.validate(); graph.has_value() == false )
            return graph;
        if ( _listPiece.size() != _graph._leafCount )
            return unexpected( Error{ ErrorKind::InvalidArgument, "piece count differs from leaf count" } );
        if ( _listVertex.size() % 3 != 0 || _listTriangleSlot.size() * 3 != _listVertex.size() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "triangle slots do not match the vertex list" } );
        uint32 expectedVertex = 0;
        for ( const FracturePiece& piece : _listPiece )
        {
            const bool bInvalidPieceVertexRange = piece._firstVertex != expectedVertex || piece._vertexCount % 3 != 0 || piece._vertexCount == 0;
            if ( bInvalidPieceVertexRange )
                return unexpected( Error{ ErrorKind::InvalidArgument, "piece vertex ranges must be contiguous triangle lists" } );
            expectedVertex += piece._vertexCount;
            if ( static_cast<size_t>( piece._firstHullPoint ) + piece._hullPointCount > _listHullPoint.size() || piece._hullPointCount < 3 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "piece hull out of range or too small" } );
        }
        if ( expectedVertex != _listVertex.size() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "vertex list longer than the pieces" } );
        for ( const uint8 slot : _listTriangleSlot )
        {
            if ( slot >= static_cast<uint8>( FractureSurfaceSlot::Count ) )
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown surface slot" } );
        }
        return {};
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
        ByteWriter writer{ outBytes };
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
        FractureAssetInternal::writeFloat3( writer, _boundsMin );
        FractureAssetInternal::writeFloat3( writer, _boundsMax );
        for ( const FractureNode& node : _graph._listNode )
        {
            FractureAssetInternal::writeFloat3( writer, node._centroid );
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
            FractureAssetInternal::writeFloat3( writer, piece._boundsMin );
            FractureAssetInternal::writeFloat3( writer, piece._boundsMax );
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
            for ( const float32 value : vertex._arrUV )
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
            FractureAssetInternal::writeFloat3( writer, point );
        }
    }

    bool FractureAsset::readFromBytes( const uint8* pData, size_t size )
    {
        clear();
        const bool bMissingFractureMagic = pData == nullptr || size < 8 || Memory::compare( pData, FractureAssetInternal::kArrMagic, 4 ) != 0;
        if ( bMissingFractureMagic )
        {
            SW_LOG_ERROR_ID( "FractureAsset.NotFractureAssetBad", "Not a fracture asset (bad magic)" );
            return false;
        }
        ByteReader reader{ pData, size };
        (void)reader.skipBytes( sizeof( FractureAssetInternal::kArrMagic ) ); // 위에서 길이와 매직을 봤다
        const uint32 version = reader.readUint32();
        if ( version != kVersion )
        {
            SW_LOG_ERROR_ID( "FractureAsset.FractureAssetVersionNot", "Fracture asset version %# is not %# - re-import the model", version, kVersion );
            return false;
        }
        const uint32 leafCount   = reader.readUint32();
        const uint32 nodeCount   = reader.readUint32();
        const uint32 childCount  = reader.readUint32();
        const uint32 linkCount   = reader.readUint32();
        const uint32 vertexCount = reader.readUint32();
        const uint32 hullCount   = reader.readUint32();
        _seed                    = reader.readUint64();
        _boundsMin               = FractureAssetInternal::readFloat3( reader );
        _boundsMax               = FractureAssetInternal::readFloat3( reader );
        if ( reader.hasOverflowed() )
        {
            SW_LOG_ERROR_ID( "FractureAsset.FractureAssetTruncated", "Fracture asset is truncated or its header holds a non-finite value" );
            clear();
            return false;
        }
        const bool bLengthsFit = FractureAssetInternal::readGraph( reader, leafCount, nodeCount, childCount, linkCount, _graph ) && FractureAssetInternal::readPieces( reader, leafCount, _listPiece ) &&
                                 FractureAssetInternal::readMesh( reader, vertexCount, hullCount, *this ) && reader.isFullyRead();

        const expected<void> valid = bLengthsFit ? validate() : expected<void>{ unexpected( Error{ ErrorKind::InvalidArgument, "length mismatch or a non-finite value" } ) };
        if ( valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "FractureAsset.FractureAssetMalformed", "Fracture asset is malformed: %#", valid.error().message() );
            clear();
            return false;
        }
        return true;
    }

    bool FractureAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false )
            return false;
        return FileUtil::writeFile( absPath.str(), bytes.data(), bytes.size() );
    }

    bool FractureAsset::loadFromAsset( const AssetID& assetID )
    {
        clear();
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
            return false;
        return readFromBytes( bytes.data(), bytes.size() );
    }

    string FractureAsset::makePathForMesh( string_view meshPath )
    {
        string path = FileUtil::removeExtension( meshPath );
        path += kExtension;
        return path;
    }

    string FractureAsset::makeCompanionPath( string_view assetID )
    {
        return StringUtil::endsWith( assetID, MeshAssetFormat::kExtension, true ) ? makePathForMesh( assetID ) : string{};
    }
} // namespace sw

SW_ASSET_COMPANION( MeshFracture, &::sw::FractureAsset::makeCompanionPath );
