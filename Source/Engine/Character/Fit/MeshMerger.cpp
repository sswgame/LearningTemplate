#include "pch.h"

#include "Engine/Character/Fit/MeshMerger.h"

#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/GeometryCut.h"

namespace sw
{
    namespace
    {
        struct MeshMergerInternal
        {
            static hashed_string getMaterialGroup( const MeshMergeSource& source, const IMeshMergeHooks* pHooks )
            {
                return pHooks != nullptr ? pHooks->getMaterialGroup( source ) : source._material;
            }

            /** @brief 부품 하나를 병합 직전 꼴로 — 피팅 델타 · 쉬는 변환 · 소켓 본 묶음 · UV 옮김을 건 뒤 보이는 삼각형만 남긴다. */
            static void prepareSource( const MeshMergeSource& source, const IMeshMergeHooks* pHooks, AppearanceGeometry& outPrepared )
            {
                AppearanceGeometry transformed = *source._pGeometry;
                const uint32       vertexCount = transformed.getVertexCount();
                if ( source._pFit != nullptr && source._pFit->_listVertexDelta.size() == vertexCount )
                {
                    for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
                    {
                        transformed._listPosition[vertex] += source._pFit->_listVertexDelta[vertex];
                    }
                }
                if ( source._socketBone >= 0 )
                {
                    for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
                    {
                        transformed._listPosition[vertex] = float3::transform( transformed._listPosition[vertex], source._restTransform );
                    }
                    for ( float3& normal : transformed._listNormal )
                    {
                        normal = CharacterGeometryUtil::makeUnitOr( float3::transformVector( normal, source._restTransform ), normal );
                    }
                    for ( GeometryMorphTarget& morph : transformed._listMorph )
                    {
                        for ( float3& delta : morph._listPositionDelta )
                        {
                            delta = float3::transformVector( delta, source._restTransform );
                        }
                    }
                    SkinInfluence rigid;
                    rigid._arrJoint[0]  = static_cast<uint16>( source._socketBone );
                    rigid._arrWeight[0] = 1.0f;
                    transformed._listSkin.assign( vertexCount, rigid );
                }
                if ( pHooks != nullptr )
                {
                    for ( float2& uv : transformed._listUv )
                    {
                        uv = pHooks->remapUv( source, uv );
                    }
                }
                vector<uint8> listVisible( transformed.getTriangleCount(), SW_TRUE );
                if ( source._pFit != nullptr )
                {
                    for ( uint32 triangle = 0; triangle < transformed.getTriangleCount(); ++triangle )
                    {
                        listVisible[triangle] = source._pFit->isTriangleVisible( triangle ) ? SW_TRUE : SW_FALSE;
                    }
                }
                transformed._listTrianglePart.clear();
                GeometryCutUtil::extractTriangles( transformed, listVisible, outPrepared );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool MeshMerger::merge( vector_reference<const MeshMergeSource> listSource, const IMeshMergeHooks* pHooks, MergedMesh& outMerged, string* pOutError )
    {
        outMerged = MergedMesh{};
        if ( listSource.empty() )
            return true;
        const uint32 skeletonID = listSource[0]._skeletonID;
        for ( const MeshMergeSource& source : listSource )
        {
            if ( source._pGeometry == nullptr || source._pGeometry->isValid() == false )
            {
                if ( pOutError != nullptr )
                    *pOutError = string( "merge part '" ) + source._name.c_str() + "' has no valid geometry";
                return false;
            }
            if ( source._skeletonID != skeletonID )
            {
                // 애니메이션 단위가 다르면 포즈 버퍼가 다르다 — 한 형상으로 그릴 수 없다.
                if ( pOutError != nullptr )
                    *pOutError = string( "merge part '" ) + source._name.c_str() + "' belongs to another skeleton (animation unit); parts merge within one unit only";
                return false;
            }
        }
        outMerged._skeletonID = skeletonID;

        // 머티리얼 묶음은 처음 나온 순서. 묶음 안의 부품은 입력 순서.
        vector<hashed_string> listGroup;
        vector<hashed_string> listSourceGroup( listSource.size() );
        for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
        {
            const hashed_string group    = MeshMergerInternal::getMaterialGroup( listSource[sourceIndex], pHooks );
            listSourceGroup[sourceIndex] = group;
            if ( std::find( listGroup.begin(), listGroup.end(), group ) == listGroup.end() )
                listGroup.push_back( group );
        }
        outMerged._listPart.resize( listSource.size() );
        for ( const hashed_string& group : listGroup )
        {
            MergedSection section;
            section._materialGroup = group;
            section._indexStart    = static_cast<uint32>( outMerged._geometry._listIndex.size() );
            for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
            {
                if ( listSourceGroup[sourceIndex] != group )
                    continue;
                const MeshMergeSource& source = listSource[sourceIndex];
                AppearanceGeometry     prepared;
                MeshMergerInternal::prepareSource( source, pHooks, prepared );
                MergedPartRange& range = outMerged._listPart[sourceIndex];
                range._name            = source._name;
                range._restTransform   = source._restTransform;
                range._socketBone      = source._socketBone;
                range._vertexStart     = outMerged._geometry.getVertexCount();
                range._vertexCount     = prepared.getVertexCount();
                GeometryCutUtil::appendGeometry( outMerged._geometry, prepared, static_cast<uint16>( sourceIndex ) );
            }
            section._indexCount = static_cast<uint32>( outMerged._geometry._listIndex.size() ) - section._indexStart;
            outMerged._listSection.push_back( section );
        }
        return true;
    }

    bool MeshMerger::extractPart( const MergedMesh& merged, const hashed_string& partName, AppearanceGeometry& outGeometry )
    {
        uint32 partIndex = 0;
        while ( partIndex < merged._listPart.size() && merged._listPart[partIndex]._name != partName )
        {
            ++partIndex;
        }
        if ( partIndex == merged._listPart.size() )
            return false;
        const AppearanceGeometry& geometry = merged._geometry;
        vector<uint8>             listSelected( geometry.getTriangleCount(), SW_FALSE );
        for ( uint32 triangle = 0; triangle < geometry.getTriangleCount() && triangle < geometry._listTrianglePart.size(); ++triangle )
        {
            listSelected[triangle] = geometry._listTrianglePart[triangle] == partIndex ? SW_TRUE : SW_FALSE;
        }
        GeometryCutUtil::extractTriangles( geometry, listSelected, outGeometry );
        outGeometry._listTrianglePart.clear();
        const MergedPartRange& range = merged._listPart[partIndex];
        if ( range._socketBone < 0 )
            return true;
        // 강체 부품 — 쉬는 변환의 역으로 부품 공간에 돌려놓고 소켓 본 묶음을 푼다.
        const float4x4 inverseRest = range._restTransform.invert();
        for ( float3& position : outGeometry._listPosition )
        {
            position = float3::transform( position, inverseRest );
        }
        for ( float3& normal : outGeometry._listNormal )
        {
            normal = CharacterGeometryUtil::makeUnitOr( float3::transformVector( normal, inverseRest ), normal );
        }
        for ( GeometryMorphTarget& morph : outGeometry._listMorph )
        {
            for ( float3& delta : morph._listPositionDelta )
            {
                delta = float3::transformVector( delta, inverseRest );
            }
        }
        outGeometry._listSkin.clear();
        return true;
    }
} // namespace sw
