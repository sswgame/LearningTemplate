#include "pch.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    int32 SkinInfluence::findDominantJoint() const
    {
        int32   bestJoint  = -1;
        float32 bestWeight = 0.0f;
        for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
        {
            if ( _arrWeight[slot] > bestWeight )
            {
                bestWeight = _arrWeight[slot];
                bestJoint  = static_cast<int32>( _arrJoint[slot] );
            }
        }
        return bestJoint;
    }
} // namespace sw

namespace sw
{
    bool AppearanceGeometry::isValid() const
    {
        const size_t vertexCount = _listPosition.size();
        if ( _listIndex.size() % 3 != 0 )
            return false;
        const bool bNormalMismatch = _listNormal.empty() == false && _listNormal.size() != vertexCount;
        const bool bUvMismatch     = _listUv.empty() == false && _listUv.size() != vertexCount;
        const bool bSkinMismatch   = _listSkin.empty() == false && _listSkin.size() != vertexCount;
        const bool bGroupMismatch  = _listVertexGroup.empty() == false && _listVertexGroup.size() != vertexCount;
        const bool bPartMismatch   = _listTrianglePart.empty() == false && _listTrianglePart.size() != getTriangleCount();
        if ( bNormalMismatch || bUvMismatch || bSkinMismatch || bGroupMismatch || bPartMismatch )
            return false;
        for ( const uint32 index : _listIndex )
        {
            if ( index >= vertexCount )
                return false;
        }
        for ( const GeometryMorphTarget& morph : _listMorph )
        {
            if ( morph._listPositionDelta.size() != vertexCount )
                return false;
        }
        return true;
    }

    uint16 AppearanceGeometry::findGroup( const hashed_string& name ) const
    {
        for ( size_t groupIndex = 0; groupIndex < _listGroupName.size(); ++groupIndex )
        {
            if ( _listGroupName[groupIndex] == name )
                return static_cast<uint16>( groupIndex );
        }
        return CharacterGeometryConstant::kNoGroup;
    }

    const GeometryMorphTarget* AppearanceGeometry::findMorph( const hashed_string& name ) const
    {
        for ( const GeometryMorphTarget& morph : _listMorph )
        {
            if ( morph._name == name )
                return &morph;
        }
        return nullptr;
    }

    void AppearanceGeometry::computeVertexNormals()
    {
        CharacterGeometryUtil::computeVertexNormals( _listPosition,
                                                     _listIndex, _listNormal );
    }

    void AppearanceGeometry::clear()
    {
        _listPosition.clear();
        _listNormal.clear();
        _listUv.clear();
        _listIndex.clear();
        _listSkin.clear();
        _listVertexGroup.clear();
        _listGroupName.clear();
        _listTrianglePart.clear();
        _listMorph.clear();
    }
} // namespace sw

namespace sw
{
    int32 CharacterBoneArray::addBone( const hashed_string& name, int32 parentIndex, const float4x4& localTransform )
    {
        const int32 boneCount = static_cast<int32>( _listName.size() );
        if ( parentIndex < -1 || parentIndex >= boneCount )
            return -1;
        _listName.push_back( name );
        _listParent.push_back( parentIndex );
        _listLocal.push_back( localTransform );
        _listModel.push_back( parentIndex < 0 ? localTransform : localTransform * _listModel[static_cast<size_t>( parentIndex )] );
        return boneCount;
    }

    int32 CharacterBoneArray::findBone( const hashed_string& name ) const
    {
        for ( size_t boneIndex = 0; boneIndex < _listName.size(); ++boneIndex )
        {
            if ( _listName[boneIndex] == name )
                return static_cast<int32>( boneIndex );
        }
        return -1;
    }

    void CharacterBoneArray::computeModelTransforms()
    {
        _listModel.resize( _listLocal.size() );
        for ( size_t boneIndex = 0; boneIndex < _listLocal.size(); ++boneIndex )
        {
            const int32 parentIndex = _listParent[boneIndex];
            _listModel[boneIndex]   = parentIndex < 0 ? _listLocal[boneIndex] : _listLocal[boneIndex] * _listModel[static_cast<size_t>( parentIndex )];
        }
    }
} // namespace sw

namespace sw
{
    float3 CharacterGeometryUtil::makeUnitOr( const float3& value, const float3& fallback )
    {
        const float32 lengthSquared = value.getLengthSquared();
        if ( lengthSquared <= MathUtil::kEpsilonSquared )
            return fallback;
        return value * ( 1.0f / MathUtil::sqrt( lengthSquared ) );
    }

    float4x4 CharacterGeometryUtil::makeTransform( const float3& translation, const quaternion& rotation, const float3& scale )
    {
        return float4x4::createTrs( translation, rotation, scale );
    }

    float4x4 CharacterGeometryUtil::blendTransforms( const float4x4& from, const float4x4& to, float32 weight )
    {
        float3     fromScale;
        float3     toScale;
        quaternion fromRotation;
        quaternion toRotation;
        float3     fromTranslation;
        float3     toTranslation;
        (void)from.decompose( fromScale, fromRotation, fromTranslation );
        (void)to.decompose( toScale, toRotation, toTranslation );
        return makeTransform( float3::lerp( fromTranslation, toTranslation, weight ), quaternion::slerp( fromRotation, toRotation, weight ),
                              float3::lerp( fromScale, toScale, weight ) );
    }

    float3 CharacterGeometryUtil::findClosestPointOnTriangle( const float3& point, const float3& a, const float3& b, const float3& c, float32& outU, float32& outV )
    {
        // Ericson, Real-Time Collision Detection 5.1.5 — 보로노이 영역으로 나눠 가장 가까운 점을 고른다.
        const float3  ab = b - a;
        const float3  ac = c - a;
        const float3  ap = point - a;
        const float32 d1 = ab.dot( ap );
        const float32 d2 = ac.dot( ap );
        if ( d1 <= 0.0f && d2 <= 0.0f )
        {
            outU = 0.0f;
            outV = 0.0f;
            return a;
        }
        const float3  bp = point - b;
        const float32 d3 = ab.dot( bp );
        const float32 d4 = ac.dot( bp );
        if ( d3 >= 0.0f && d4 <= d3 )
        {
            outU = 1.0f;
            outV = 0.0f;
            return b;
        }
        const float32 vc = d1 * d4 - d3 * d2;
        if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f )
        {
            const float32 edgeWeight = d1 / ( d1 - d3 );
            outU                     = edgeWeight;
            outV                     = 0.0f;
            return a + ab * edgeWeight;
        }
        const float3  cp = point - c;
        const float32 d5 = ab.dot( cp );
        const float32 d6 = ac.dot( cp );
        if ( d6 >= 0.0f && d5 <= d6 )
        {
            outU = 0.0f;
            outV = 1.0f;
            return c;
        }
        const float32 vb = d5 * d2 - d1 * d6;
        if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f )
        {
            const float32 edgeWeight = d2 / ( d2 - d6 );
            outU                     = 0.0f;
            outV                     = edgeWeight;
            return a + ac * edgeWeight;
        }
        const float32 va = d3 * d6 - d5 * d4;
        if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f )
        {
            const float32 edgeWeight = ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) );
            outU                     = 1.0f - edgeWeight;
            outV                     = edgeWeight;
            return b + ( c - b ) * edgeWeight;
        }
        const float32 denominator = 1.0f / ( va + vb + vc );
        outU                      = vb * denominator;
        outV                      = vc * denominator;
        return a + ab * outU + ac * outV;
    }

    bool CharacterGeometryUtil::intersectRayTriangle( const float3& origin, const float3& direction, const float3& a, const float3& b, const float3& c,
                                                      float32& outDistance, float32& outU, float32& outV, bool& outFrontFace )
    {
        // Möller–Trumbore, 양면. 무게중심 경계에 작은 여유를 둔다 — 광선이 두 삼각형이 나누는 모서리를 정확히 지나면 부동소수 오차로
        // 양쪽 다 빗나갈 수 있다(같은 각도로 나뉜 원기둥 두 겹에서 실제로 났다). 여유 안의 값은 경계로 묶는다.
        constexpr float32 kBarycentricSlack = 1.0e-5f;
        const float3      edge1             = b - a;
        const float3      edge2             = c - a;
        const float3      pVector           = direction.cross( edge2 );
        const float32     determinant       = edge1.dot( pVector );
        if ( MathUtil::abs( determinant ) < 1.0e-12f )
            return false;
        const float32 inverseDeterminant = 1.0f / determinant;
        const float3  tVector            = origin - a;
        float32       u                  = tVector.dot( pVector ) * inverseDeterminant;
        if ( u < -kBarycentricSlack || u > 1.0f + kBarycentricSlack )
            return false;
        const float3 qVector = tVector.cross( edge1 );
        float32      v       = direction.dot( qVector ) * inverseDeterminant;
        if ( v < -kBarycentricSlack || u + v > 1.0f + kBarycentricSlack )
            return false;
        u                      = MathUtil::saturate( u );
        v                      = MathUtil::clamp( v, 0.0f, 1.0f - u );
        const float32 distance = edge2.dot( qVector ) * inverseDeterminant;
        if ( distance < 0.0f )
            return false;
        outDistance  = distance;
        outU         = u;
        outV         = v;
        outFrontFace = direction.dot( edge1.cross( edge2 ) ) < 0.0f;
        return true;
    }

    float3 CharacterGeometryUtil::interpolateBarycentric( const float3& a, const float3& b, const float3& c, float32 u, float32 v )
    {
        return a * ( 1.0f - u - v ) + b * u + c * v;
    }

    float32 CharacterGeometryUtil::computeFalloff( float32 normalizedDistance )
    {
        if ( normalizedDistance <= 0.0f )
            return 1.0f;
        if ( normalizedDistance >= 1.0f )
            return 0.0f;
        const float32 rising = normalizedDistance * normalizedDistance * ( 3.0f - 2.0f * normalizedDistance );
        return 1.0f - rising;
    }

    void CharacterGeometryUtil::computeVertexNormals( vector_reference<const float3> listPosition, vector_reference<const uint32> listIndex, vector<float3>& outListNormal )
    {
        outListNormal.assign( listPosition.size(), float3::Zero );
        for ( size_t corner = 0; corner + 2 < listIndex.size(); corner += 3 )
        {
            const uint32 indexA = listIndex[corner];
            const uint32 indexB = listIndex[corner + 1];
            const uint32 indexC = listIndex[corner + 2];
            // 외적의 길이가 면적의 두 배라 그대로 더하면 면적 가중이다.
            const float3 faceNormal = ( listPosition[indexB] - listPosition[indexA] ).cross( listPosition[indexC] - listPosition[indexA] );
            outListNormal[indexA] += faceNormal;
            outListNormal[indexB] += faceNormal;
            outListNormal[indexC] += faceNormal;
        }
        for ( float3& normal : outListNormal )
        {
            normal = makeUnitOr( normal, float3::UnitY );
        }
    }
} // namespace sw
