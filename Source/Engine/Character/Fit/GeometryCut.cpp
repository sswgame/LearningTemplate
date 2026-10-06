#include "pch.h"

#include "Engine/Character/Fit/GeometryCut.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct GeometryCutInternal
        {
            static constexpr uint32  kNoVertex       = 0xFFFFFFFFu;
            static constexpr float32 kWeldResolution = 1.0e-5f;

            static uint64 makeEdgeKey( uint32 from, uint32 to ) { return ( static_cast<uint64>( from ) << 32 ) | static_cast<uint64>( to ); }

            /**
             * @brief 자리를 격자로 반올림한 키 — 용접(같은 자리 = 같은 정점).
             * @details 성분을 차례로 섞어 잇는다. 성분마다 곱해 XOR 로 합치면 부호만 다른 대칭 꼭짓점(±1 · ±0.5 · ±0.25 상자)이 같은 키가 되어
             *          서로 다른 꼭짓점이 하나로 용접된다.
             */
            static uint64 makePositionKey( const float3& position )
            {
                const int64 quantX = static_cast<int64>( MathUtil::round( position._x / kWeldResolution ) );
                const int64 quantY = static_cast<int64>( MathUtil::round( position._y / kWeldResolution ) );
                const int64 quantZ = static_cast<int64>( MathUtil::round( position._z / kWeldResolution ) );
                uint64      hash   = HashUtil::mix64( static_cast<uint64>( quantX ) + HashUtil::kGoldenRatio64 );
                hash               = HashUtil::mix64( hash ^ ( static_cast<uint64>( quantY ) + 0xC2B2AE3D27D4EB4Full ) );
                return HashUtil::mix64( hash ^ ( static_cast<uint64>( quantZ ) + 0x165667B19E3779F9ull ) );
            }

            static uint16 findOrAddGroup( AppearanceGeometry& inoutTarget, const hashed_string& name )
            {
                const uint16 found = inoutTarget.findGroup( name );
                if ( found != CharacterGeometryConstant::kNoGroup )
                    return found;
                inoutTarget._listGroupName.push_back( name );
                return static_cast<uint16>( inoutTarget._listGroupName.size() - 1 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GeometryCutUtil::extractTriangles( const AppearanceGeometry& source, vector_reference<const uint8> listTriangleSelected, AppearanceGeometry& outGeometry,
                                            vector<uint32>* pOutListVertexRemap )
    {
        outGeometry.clear();
        outGeometry._listGroupName   = source._listGroupName;
        const uint32   vertexCount   = source.getVertexCount();
        const uint32   triangleCount = MathUtil::min( source.getTriangleCount(), static_cast<uint32>( listTriangleSelected.size() ) );
        vector<uint32> listRemap( vertexCount, GeometryCutInternal::kNoVertex );
        // 남는 정점은 원래 순서를 지킨다 — 다시 떼어 낸 부품이 원본과 같은 정점 번호를 갖게.
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            if ( listTriangleSelected[triangle] == 0 )
                continue;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                listRemap[source._listIndex[triangle * 3 + corner]] = 0;
            }
        }
        vector<uint32> listKeptVertex;
        for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
        {
            if ( listRemap[vertex] == GeometryCutInternal::kNoVertex )
                continue;
            listRemap[vertex] = static_cast<uint32>( listKeptVertex.size() );
            listKeptVertex.push_back( vertex );
        }
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            if ( listTriangleSelected[triangle] == 0 )
                continue;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                outGeometry._listIndex.push_back( listRemap[source._listIndex[triangle * 3 + corner]] );
            }
            if ( source._listTrianglePart.size() == source.getTriangleCount() )
                outGeometry._listTrianglePart.push_back( source._listTrianglePart[triangle] );
        }
        const bool bHasNormal = source._listNormal.size() == vertexCount;
        const bool bHasUv     = source._listUv.size() == vertexCount;
        const bool bHasSkin   = source._listSkin.size() == vertexCount;
        const bool bHasGroup  = source._listVertexGroup.size() == vertexCount;
        for ( const uint32 vertex : listKeptVertex )
        {
            outGeometry._listPosition.push_back( source._listPosition[vertex] );
            if ( bHasNormal )
                outGeometry._listNormal.push_back( source._listNormal[vertex] );
            if ( bHasUv )
                outGeometry._listUv.push_back( source._listUv[vertex] );
            if ( bHasSkin )
                outGeometry._listSkin.push_back( source._listSkin[vertex] );
            if ( bHasGroup )
                outGeometry._listVertexGroup.push_back( source._listVertexGroup[vertex] );
        }
        for ( const GeometryMorphTarget& morph : source._listMorph )
        {
            GeometryMorphTarget extracted;
            extracted._name            = morph._name;
            const bool bHasNormalDelta = morph._listNormalDelta.size() == vertexCount;
            for ( const uint32 vertex : listKeptVertex )
            {
                extracted._listPositionDelta.push_back( morph._listPositionDelta[vertex] );
                if ( bHasNormalDelta )
                    extracted._listNormalDelta.push_back( morph._listNormalDelta[vertex] );
            }
            outGeometry._listMorph.push_back( std::move( extracted ) );
        }
        if ( pOutListVertexRemap != nullptr )
            *pOutListVertexRemap = std::move( listRemap );
    }

    void GeometryCutUtil::splitByTriangleMask( const AppearanceGeometry& source, vector_reference<const uint8> listTriangleSevered, AppearanceGeometry& outKept,
                                               AppearanceGeometry& outSevered )
    {
        vector<uint8> listKept( source.getTriangleCount(), SW_TRUE );
        for ( uint32 triangle = 0; triangle < source.getTriangleCount() && triangle < listTriangleSevered.size(); ++triangle )
        {
            listKept[triangle] = listTriangleSevered[triangle] != 0 ? SW_FALSE : SW_TRUE;
        }
        vector<uint8> listSevered( source.getTriangleCount(), SW_FALSE );
        for ( uint32 triangle = 0; triangle < source.getTriangleCount(); ++triangle )
        {
            listSevered[triangle] = listKept[triangle] == SW_TRUE ? SW_FALSE : SW_TRUE;
        }
        extractTriangles( source, listKept, outKept );
        extractTriangles( source, listSevered, outSevered );
    }

    void GeometryCutUtil::appendGeometry( AppearanceGeometry& inoutTarget, const AppearanceGeometry& source, uint16 trianglePart )
    {
        const uint32 baseVertex        = inoutTarget.getVertexCount();
        const uint32 sourceVertexCount = source.getVertexCount();
        const uint32 targetTriangles   = inoutTarget.getTriangleCount();
        // 선택 칸은 한쪽에만 있어도 맞춰 채운다(없는 쪽은 기본값).
        const bool bAnyNormal = inoutTarget._listNormal.empty() == false || source._listNormal.empty() == false;
        const bool bAnyUv     = inoutTarget._listUv.empty() == false || source._listUv.empty() == false;
        const bool bAnySkin   = inoutTarget._listSkin.empty() == false || source._listSkin.empty() == false;
        const bool bAnyGroup  = inoutTarget._listVertexGroup.empty() == false || source._listVertexGroup.empty() == false;
        if ( bAnyNormal )
            inoutTarget._listNormal.resize( baseVertex, float3::UnitY );
        if ( bAnyUv )
            inoutTarget._listUv.resize( baseVertex, float2::Zero );
        if ( bAnySkin )
            inoutTarget._listSkin.resize( baseVertex, SkinInfluence{} );
        if ( bAnyGroup )
            inoutTarget._listVertexGroup.resize( baseVertex, CharacterGeometryConstant::kNoGroup );
        const bool bAnyPart = inoutTarget._listTrianglePart.empty() == false || source._listTrianglePart.empty() == false ||
                              trianglePart != CharacterGeometryConstant::kNoPart;
        if ( bAnyPart )
            inoutTarget._listTrianglePart.resize( targetTriangles, CharacterGeometryConstant::kNoPart );

        for ( uint32 vertex = 0; vertex < sourceVertexCount; ++vertex )
        {
            inoutTarget._listPosition.push_back( source._listPosition[vertex] );
            if ( bAnyNormal )
                inoutTarget._listNormal.push_back( source._listNormal.size() == sourceVertexCount ? source._listNormal[vertex] : float3::UnitY );
            if ( bAnyUv )
                inoutTarget._listUv.push_back( source._listUv.size() == sourceVertexCount ? source._listUv[vertex] : float2::Zero );
            if ( bAnySkin )
                inoutTarget._listSkin.push_back( source._listSkin.size() == sourceVertexCount ? source._listSkin[vertex] : SkinInfluence{} );
            if ( bAnyGroup )
            {
                uint16 group = CharacterGeometryConstant::kNoGroup;
                if ( source._listVertexGroup.size() == sourceVertexCount && source._listVertexGroup[vertex] < source._listGroupName.size() )
                    group = GeometryCutInternal::findOrAddGroup( inoutTarget, source._listGroupName[source._listVertexGroup[vertex]] );
                inoutTarget._listVertexGroup.push_back( group );
            }
        }
        for ( const uint32 index : source._listIndex )
        {
            inoutTarget._listIndex.push_back( baseVertex + index );
        }
        if ( bAnyPart )
        {
            for ( uint32 triangle = 0; triangle < source.getTriangleCount(); ++triangle )
            {
                const bool bSourceHasPart = source._listTrianglePart.size() == source.getTriangleCount();
                inoutTarget._listTrianglePart.push_back( trianglePart != CharacterGeometryConstant::kNoPart ? trianglePart
                                                                                                            : ( bSourceHasPart ? source._listTrianglePart[triangle]
                                                                                                                               : CharacterGeometryConstant::kNoPart ) );
            }
        }
        // 모프는 이름으로 — 한쪽에만 있는 모프는 다른 쪽 구간이 0.
        for ( GeometryMorphTarget& targetMorph : inoutTarget._listMorph )
        {
            targetMorph._listPositionDelta.resize( baseVertex, float3::Zero );
            const GeometryMorphTarget* pSourceMorph = source.findMorph( targetMorph._name );
            for ( uint32 vertex = 0; vertex < sourceVertexCount; ++vertex )
            {
                targetMorph._listPositionDelta.push_back( pSourceMorph != nullptr ? pSourceMorph->_listPositionDelta[vertex] : float3::Zero );
            }
            if ( targetMorph._listNormalDelta.empty() == false )
            {
                targetMorph._listNormalDelta.resize( baseVertex, float3::Zero );
                const bool bSourceNormal = pSourceMorph != nullptr && pSourceMorph->_listNormalDelta.size() == sourceVertexCount;
                for ( uint32 vertex = 0; vertex < sourceVertexCount; ++vertex )
                {
                    targetMorph._listNormalDelta.push_back( bSourceNormal ? pSourceMorph->_listNormalDelta[vertex] : float3::Zero );
                }
            }
        }
        for ( const GeometryMorphTarget& sourceMorph : source._listMorph )
        {
            bool bExists = false;
            for ( const GeometryMorphTarget& targetMorph : inoutTarget._listMorph )
            {
                if ( targetMorph._name == sourceMorph._name )
                    bExists = true;
            }
            if ( bExists )
                continue;
            GeometryMorphTarget added;
            added._name = sourceMorph._name;
            added._listPositionDelta.assign( baseVertex, float3::Zero );
            added._listPositionDelta.insert( added._listPositionDelta.end(), sourceMorph._listPositionDelta.begin(), sourceMorph._listPositionDelta.end() );
            if ( sourceMorph._listNormalDelta.size() == sourceVertexCount )
            {
                added._listNormalDelta.assign( baseVertex, float3::Zero );
                added._listNormalDelta.insert( added._listNormalDelta.end(), sourceMorph._listNormalDelta.begin(), sourceMorph._listNormalDelta.end() );
            }
            inoutTarget._listMorph.push_back( std::move( added ) );
        }
    }

    void GeometryCutUtil::findBoundaryLoops( const AppearanceGeometry& geometry, vector<vector<uint32>>& outListLoop )
    {
        outListLoop.clear();
        // 같은 자리 정점을 하나로 본다 — UV 이음매로 갈라진 정점이 가짜 경계를 만들지 않게.
        const uint32                  vertexCount = geometry.getVertexCount();
        vector<uint32>                listWelded( vertexCount );
        unordered_map<uint64, uint32> mapPositionToVertex;
        for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
        {
            const uint64 key   = GeometryCutInternal::makePositionKey( geometry._listPosition[vertex] );
            const auto   found = mapPositionToVertex.find( key );
            if ( found == mapPositionToVertex.end() )
            {
                mapPositionToVertex.emplace( key, vertex );
                listWelded[vertex] = vertex;
            }
            else
            {
                listWelded[vertex] = found->second;
            }
        }
        unordered_map<uint64, uint32> mapEdgeCount;
        for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint32 from = listWelded[geometry._listIndex[triangle * 3 + corner]];
                const uint32 to   = listWelded[geometry._listIndex[triangle * 3 + ( corner + 1 ) % 3]];
                ++mapEdgeCount[GeometryCutInternal::makeEdgeKey( from, to )];
            }
        }
        // 경계 모서리 = 반대 방향 모서리가 없는 것. 시작 정점 → 끝 정점으로 잇는다.
        unordered_map<uint32, uint32> mapNext;
        vector<uint32>                listStart;
        for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint32 from = listWelded[geometry._listIndex[triangle * 3 + corner]];
                const uint32 to   = listWelded[geometry._listIndex[triangle * 3 + ( corner + 1 ) % 3]];
                if ( mapEdgeCount.find( GeometryCutInternal::makeEdgeKey( to, from ) ) != mapEdgeCount.end() )
                    continue;
                if ( mapNext.find( from ) != mapNext.end() )
                    continue; // 나비 모양(한 정점에 경계 둘) — 첫 것만 잇는다
                mapNext.emplace( from, to );
                listStart.push_back( from );
            }
        }
        unordered_map<uint32, uint8> mapVisited;
        for ( const uint32 start : listStart )
        {
            if ( mapVisited.find( start ) != mapVisited.end() )
                continue;
            vector<uint32> listLoopVertex;
            uint32         current = start;
            for ( size_t step = 0; step <= mapNext.size(); ++step )
            {
                if ( mapVisited.find( current ) != mapVisited.end() )
                    break;
                mapVisited.emplace( current, static_cast<uint8>( SW_TRUE ) );
                listLoopVertex.push_back( current );
                const auto next = mapNext.find( current );
                if ( next == mapNext.end() )
                    break;
                current = next->second;
            }
            if ( listLoopVertex.size() >= 3 && current == start )
                outListLoop.push_back( std::move( listLoopVertex ) );
        }
    }

    uint32 GeometryCutUtil::createCaps( const AppearanceGeometry& geometry, AppearanceGeometry& outCap, const vector<uint8>* pVertexOnSeam )
    {
        outCap.clear();
        vector<vector<uint32>> listLoop;
        findBoundaryLoops( geometry, listLoop );
        const bool bHasSkin    = geometry.isSkinned();
        uint32     cappedCount = 0;
        for ( const vector<uint32>& loop : listLoop )
        {
            if ( pVertexOnSeam != nullptr )
            {
                bool bOnSeam = false;
                for ( const uint32 vertex : loop )
                {
                    if ( vertex < pVertexOnSeam->size() && ( *pVertexOnSeam )[vertex] != 0 )
                        bOnSeam = true;
                }
                if ( bOnSeam == false )
                    continue;
            }
            ++cappedCount;
            float3 center = float3::Zero;
            for ( const uint32 vertex : loop )
            {
                center += geometry._listPosition[vertex];
            }
            center *= 1.0f / static_cast<float32>( loop.size() );
            // 뉴웰 법선 — 경계 감음(원래 면 기준)의 반대가 캡의 바깥쪽이다.
            float3 newell = float3::Zero;
            for ( size_t corner = 0; corner < loop.size(); ++corner )
            {
                const float3& current = geometry._listPosition[loop[corner]];
                const float3& next    = geometry._listPosition[loop[( corner + 1 ) % loop.size()]];
                newell += ( current - center ).cross( next - center );
            }
            const float3 capNormal = CharacterGeometryUtil::makeUnitOr( -newell, float3::UnitY );
            const float3 tangent   = CharacterGeometryUtil::makeUnitOr( geometry._listPosition[loop.front()] - center, float3::UnitX );
            const float3 bitangent = capNormal.cross( tangent );
            float32      radius    = 0.0f;
            for ( const uint32 vertex : loop )
            {
                radius = MathUtil::max( radius, float3::getDistance( geometry._listPosition[vertex], center ) );
            }
            const float32 uvScale    = radius > 0.0f ? 0.5f / radius : 0.0f;
            const uint32  baseVertex = outCap.getVertexCount();
            outCap._listPosition.push_back( center );
            outCap._listNormal.push_back( capNormal );
            outCap._listUv.push_back( float2( 0.5f, 0.5f ) );
            if ( bHasSkin )
                outCap._listSkin.push_back( geometry._listSkin[loop.front()] );
            for ( const uint32 vertex : loop )
            {
                const float3 offset = geometry._listPosition[vertex] - center;
                outCap._listPosition.push_back( geometry._listPosition[vertex] );
                outCap._listNormal.push_back( capNormal );
                outCap._listUv.push_back( float2( 0.5f + offset.dot( tangent ) * uvScale, 0.5f + offset.dot( bitangent ) * uvScale ) );
                if ( bHasSkin )
                    outCap._listSkin.push_back( geometry._listSkin[vertex] );
            }
            const uint32 loopCount = static_cast<uint32>( loop.size() );
            for ( uint32 corner = 0; corner < loopCount; ++corner )
            {
                // 경계 모서리 (a → b) 를 캡은 (b → a) 로 지나야 원래 면과 맞물려 닫힌다.
                const uint32 vertexA = baseVertex + 1 + corner;
                const uint32 vertexB = baseVertex + 1 + ( corner + 1 ) % loopCount;
                outCap._listIndex.push_back( vertexB );
                outCap._listIndex.push_back( vertexA );
                outCap._listIndex.push_back( baseVertex );
            }
        }
        return cappedCount;
    }

    bool GeometryCutUtil::isClosed( const AppearanceGeometry& geometry )
    {
        if ( geometry.getTriangleCount() == 0 )
            return false;
        unordered_map<uint64, uint32> mapPositionToVertex;
        vector<uint32>                listWelded( geometry.getVertexCount() );
        for ( uint32 vertex = 0; vertex < geometry.getVertexCount(); ++vertex )
        {
            const uint64 key   = GeometryCutInternal::makePositionKey( geometry._listPosition[vertex] );
            const auto   found = mapPositionToVertex.find( key );
            listWelded[vertex] = found == mapPositionToVertex.end() ? vertex : found->second;
            if ( found == mapPositionToVertex.end() )
                mapPositionToVertex.emplace( key, vertex );
        }
        // 방향 있는 모서리가 각각 한 번, 그 반대가 한 번 — 감음까지 맞는 닫힌 면.
        unordered_map<uint64, uint32> mapEdgeCount;
        for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint32 from = listWelded[geometry._listIndex[triangle * 3 + corner]];
                const uint32 to   = listWelded[geometry._listIndex[triangle * 3 + ( corner + 1 ) % 3]];
                if ( from == to )
                    return false;
                ++mapEdgeCount[GeometryCutInternal::makeEdgeKey( from, to )];
            }
        }
        for ( const auto& [edgeKey, count] : mapEdgeCount )
        {
            const uint32 from     = static_cast<uint32>( edgeKey >> 32 );
            const uint32 to       = static_cast<uint32>( edgeKey & 0xFFFFFFFFu );
            const auto   opposite = mapEdgeCount.find( GeometryCutInternal::makeEdgeKey( to, from ) );
            if ( count != 1 || opposite == mapEdgeCount.end() || opposite->second != 1 )
                return false;
        }
        return true;
    }
} // namespace sw
