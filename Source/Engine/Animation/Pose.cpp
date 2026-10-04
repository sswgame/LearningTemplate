#include "pch.h"

#include "Engine/Animation/Pose.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"

namespace sw
{
    namespace
    {
        struct PoseInternal
        {
            /** @brief 성분별 곱입니다. */
            static float3 multiply( const float3& lhs, const float3& rhs ) { return float3{ lhs._x * rhs._x, lhs._y * rhs._y, lhs._z * rhs._z }; }

            /** @brief 성분별 나눗셈입니다. 0 에 가까운 분모는 1 로 봅니다(스케일 0 인 레퍼런스는 비율이 없다). */
            static float3 divide( const float3& lhs, const float3& rhs )
            {
                const float32 x = MathUtil::abs( rhs._x ) > MathUtil::Epsilon ? lhs._x / rhs._x : 1.0f;
                const float32 y = MathUtil::abs( rhs._y ) > MathUtil::Epsilon ? lhs._y / rhs._y : 1.0f;
                const float32 z = MathUtil::abs( rhs._z ) > MathUtil::Epsilon ? lhs._z / rhs._z : 1.0f;
                return float3{ x, y, z };
            }

            /** @brief 가산 회전을 가중치만큼 줄입니다 — 단위 회전에서 짧은 쪽 nlerp 입니다. */
            static quaternion scaleRotation( const quaternion& delta, float32 weight )
            {
                return ( weight >= 1.0f ) ? delta : quaternion::lerp( quaternion::Identity, delta, weight );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoneTransform BoneTransform::makeFromMatrix( const float4x4& matrix )
    {
        BoneTransform transform{};
        (void)matrix.decompose( transform._scale, transform._rotation, transform._translation );
        return transform;
    }

    BoneTransform BoneTransform::blend( const BoneTransform& from, const BoneTransform& to, float32 weight )
    {
        BoneTransform result{};
        result._translation = float3::lerp( from._translation, to._translation, weight );
        result._rotation    = quaternion::lerp( from._rotation, to._rotation, weight );
        result._scale       = float3::lerp( from._scale, to._scale, weight );
        return result;
    }

    void Pose::resize( uint32 boneCount )
    {
        _listTranslation.resize( boneCount, float3{} );
        _listRotation.resize( boneCount, quaternion::Identity );
        _listScale.resize( boneCount, float3{ 1.0f, 1.0f, 1.0f } );
    }

    void Pose::setToIdentity()
    {
        for ( size_t index = 0; index < _listTranslation.size(); ++index )
        {
            _listTranslation[index] = float3{};
            _listRotation[index]    = quaternion::Identity;
            _listScale[index]       = float3{ 1.0f, 1.0f, 1.0f };
        }
    }

    void Pose::setToReference( const Skeleton& skeleton )
    {
        const uint32 boneCount = skeleton.getBoneCount();
        resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            setBoneTransform( boneIndex, skeleton.getBone( boneIndex )._referencePose );
    }

    BoneTransform Pose::getBoneTransform( uint32 boneIndex ) const
    {
        BoneTransform transform{};
        if ( boneIndex >= getBoneCount() )
            return transform;
        transform._translation = _listTranslation[boneIndex];
        transform._rotation    = _listRotation[boneIndex];
        transform._scale       = _listScale[boneIndex];
        return transform;
    }

    void Pose::setBoneTransform( uint32 boneIndex, const BoneTransform& transform )
    {
        if ( boneIndex >= getBoneCount() )
            return;
        _listTranslation[boneIndex] = transform._translation;
        _listRotation[boneIndex]    = transform._rotation;
        _listScale[boneIndex]       = transform._scale;
    }

    void Pose::blend( const Pose& from, const Pose& to, float32 weight, Pose& outPose )
    {
        blendMasked( from, to, weight, nullptr, outPose );
    }

    void Pose::blendMasked( const Pose& base, const Pose& layer, float32 weight, const float32* pBoneWeight, Pose& outPose )
    {
        const uint32 boneCount = MathUtil::min( base.getBoneCount(), layer.getBoneCount() );
        outPose.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const float32 boneWeight            = ( pBoneWeight != nullptr ) ? weight * pBoneWeight[boneIndex] : weight;
            outPose._listTranslation[boneIndex] = float3::lerp( base._listTranslation[boneIndex], layer._listTranslation[boneIndex], boneWeight );
            outPose._listRotation[boneIndex]    = quaternion::lerp( base._listRotation[boneIndex], layer._listRotation[boneIndex], boneWeight );
            outPose._listScale[boneIndex]       = float3::lerp( base._listScale[boneIndex], layer._listScale[boneIndex], boneWeight );
        }
    }

    void Pose::makeAdditive( const Pose& pose, const Pose& referencePose, Pose& outAdditive )
    {
        const uint32 boneCount = MathUtil::min( pose.getBoneCount(), referencePose.getBoneCount() );
        outAdditive.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            outAdditive._listTranslation[boneIndex] = pose._listTranslation[boneIndex] - referencePose._listTranslation[boneIndex];
            outAdditive._listRotation[boneIndex]    = ( referencePose._listRotation[boneIndex].inverse() * pose._listRotation[boneIndex] ).normalize();
            outAdditive._listScale[boneIndex]       = PoseInternal::divide( pose._listScale[boneIndex], referencePose._listScale[boneIndex] );
        }
    }

    void Pose::applyAdditive( const Pose& additive, float32 weight, const float32* pBoneWeight )
    {
        const uint32 boneCount = MathUtil::min( getBoneCount(), additive.getBoneCount() );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const float32 boneWeight = ( pBoneWeight != nullptr ) ? weight * pBoneWeight[boneIndex] : weight;
            if ( boneWeight <= 0.0f )
                continue;
            _listTranslation[boneIndex] = _listTranslation[boneIndex] + additive._listTranslation[boneIndex] * boneWeight;
            _listRotation[boneIndex]    = ( _listRotation[boneIndex] * PoseInternal::scaleRotation( additive._listRotation[boneIndex], boneWeight ) ).normalize();
            const float3 scaleRatio     = float3::lerp( float3{ 1.0f, 1.0f, 1.0f }, additive._listScale[boneIndex], boneWeight );
            _listScale[boneIndex]       = PoseInternal::multiply( _listScale[boneIndex], scaleRatio );
        }
    }

    void Pose::computeModelSpace( const vector<int32>& listParent, vector<float4x4>& outListModel ) const
    {
        const uint32 boneCount = MathUtil::min( getBoneCount(), static_cast<uint32>( listParent.size() ) );
        outListModel.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const float4x4 local       = float4x4::createTrs( _listTranslation[boneIndex], _listRotation[boneIndex], _listScale[boneIndex] );
            const int32    parentIndex = listParent[boneIndex];
            // 행벡터 규약: 자식의 로컬이 먼저, 부모의 모델 공간이 나중이다.
            if ( 0 <= parentIndex && static_cast<uint32>( parentIndex ) < boneIndex )
                outListModel[boneIndex] = local * outListModel[static_cast<uint32>( parentIndex )];
            else
                outListModel[boneIndex] = local;
        }
    }

    void Pose::computeSkinPalette( const Skeleton& skeleton, const vector<float4x4>& listModel, vector<float4x4>& outListPalette )
    {
        const uint32 boneCount = MathUtil::min( skeleton.getBoneCount(), static_cast<uint32>( listModel.size() ) );
        outListPalette.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            outListPalette[boneIndex] = skeleton.getBone( boneIndex )._inverseBind * listModel[boneIndex];
    }
} // namespace sw
