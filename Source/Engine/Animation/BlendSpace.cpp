#include "pch.h"

#include "Engine/Animation/BlendSpace.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

#include "Engine/Animation/DualQuaternion.h"
#include "Engine/Animation/Skeleton.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 포즈 행렬과 (스케일 + 강체 변환) 사이를 오갑니다.
         * @details `DualQuaternion` 은 스케일을 담지 못합니다. 그래서 포즈를 섞을 때는 스케일을
         *          따로 떼어 선형 보간하고, 나머지 회전 · 이동만 DLB 로 섞은 뒤 다시 곱합니다.
         *          떼지 않고 섞으면 표본 지점에서는 원본 포즈가 그대로 나오는데 그 사이에서만
         *          스케일이 1 로 주저앉아, 파라미터를 조금 옮기는 것만으로 포즈가 튑니다.
         */
        struct BlendSpaceInternal
        {
            /** @brief 포즈에서 스케일을 떼어 @p outScale 에 담고, 남은 강체 변환을 반환합니다. */
            static DualQuaternion splitPose( const float4x4& pose, float3& outScale )
            {
                quaternion rotation{};
                float3     translation{};
                // 축 하나가 0 이면 회전을 뽑을 수 없어 decompose 가 Identity 를 준다. 그래도
                // 스케일 0 은 그대로 살려 둔다. 눌린 포즈를 1 로 되살리면 그것대로 틀린 그림이다.
                pose.decompose( outScale, rotation, translation );
                return DualQuaternion( rotation, translation );
            }

            /** @brief 강체 변환과 스케일을 포즈 행렬로 되돌립니다. */
            static float4x4 makePose( const DualQuaternion& rigid, const float3& scale )
            {
                return float4x4::createScale( scale ) * rigid.toMatrix4x4();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void BlendSpace1D::addSample( float32 parameter, string_view clipName, const float4x4& pose )
    {
        BlendSample1D sample{};
        sample._parameter = parameter;
        sample._clipName  = string{ clipName };
        sample._pose      = pose;

        _listSample.push_back( std::move( sample ) );

        std::sort( _listSample.begin(), _listSample.end(), []( const BlendSample1D& a, const BlendSample1D& b )
        {
            return a._parameter < b._parameter;
        } );
    }

    float4x4 BlendSpace1D::evaluate( float32 parameter ) const
    {
        if ( _listSample.empty() )
            return float4x4::Identity;

        if ( _listSample.size() == 1 || parameter <= _listSample.front()._parameter )
            return _listSample.front()._pose;

        if ( parameter >= _listSample.back()._parameter )
            return _listSample.back()._pose;

        for ( size_t index = 0; index + 1 < _listSample.size(); ++index )
        {
            const BlendSample1D& s0 = _listSample[index];
            const BlendSample1D& s1 = _listSample[index + 1];

            if ( s0._parameter <= parameter && parameter <= s1._parameter )
            {
                const float32 span = s1._parameter - s0._parameter;
                const float32 t    = span > MathUtil::Epsilon ? ( ( parameter - s0._parameter ) / span ) : 0.0f;

                float3               scale0{};
                float3               scale1{};
                const DualQuaternion dq0 = BlendSpaceInternal::splitPose( s0._pose, scale0 );
                const DualQuaternion dq1 = BlendSpaceInternal::splitPose( s1._pose, scale1 );
                return BlendSpaceInternal::makePose( DualQuaternion::dlb( dq0, dq1, t ), float3::lerp( scale0, scale1, t ) );
            }
        }

        return _listSample.back()._pose;
    }

    void BlendSpace1D::evaluateSkeleton( float32 parameter, Skeleton& inoutSkeleton ) const
    {
        const float4x4 rootTransform = evaluate( parameter );
        if ( inoutSkeleton.getBoneCount() > 0 )
        {
            inoutSkeleton.setBoneSpaceTransform( 0, rootTransform );
            inoutSkeleton.updateCharacterSpaceTransforms();
        }
    }

    void BlendSpace2D::addSample( float32 paramX, float32 paramY, string_view clipName, const float4x4& pose )
    {
        BlendSample2D sample{};
        sample._parameter = float2{ paramX, paramY };
        sample._clipName  = string{ clipName };
        sample._pose      = pose;

        _listSample.push_back( std::move( sample ) );
    }

    float4x4 BlendSpace2D::evaluate( float32 paramX, float32 paramY ) const
    {
        if ( _listSample.empty() )
            return float4x4::Identity;

        if ( _listSample.size() == 1 )
            return _listSample.front()._pose;

        // 역거리 가중치(IDW). 표본 수에 상한이 없다. 예전에는 가중치를 `float[32]` 에 담고
        // 표본 수를 그 길이로 min 해서, 33번째 표본부터 한 마디 없이 버렸다. 거리 제곱을
        // 두 번 구하는 대신 그 고정 버퍼를 없앴다.
        const float2 targetParam{ paramX, paramY };
        float32      totalWeight = 0.0f;
        for ( const BlendSample2D& sample : _listSample )
        {
            const float32 distSq = float2::getDistanceSquared( targetParam, sample._parameter );
            if ( distSq < MathUtil::Epsilon )
                return sample._pose;
            totalWeight += 1.0f / distSq;
        }

        if ( totalWeight < MathUtil::Epsilon )
            return _listSample.front()._pose;

        const float32 invTotalWeight = 1.0f / totalWeight;

        float3         sampleScale{};
        DualQuaternion accumDq     = BlendSpaceInternal::splitPose( _listSample.front()._pose, sampleScale );
        float32        accumWeight = invTotalWeight / float2::getDistanceSquared( targetParam, _listSample.front()._parameter );
        float3         accumScale  = sampleScale * accumWeight;

        for ( size_t index = 1; index < _listSample.size(); ++index )
        {
            const BlendSample2D& sample           = _listSample[index];
            const float32        normalizedWeight = invTotalWeight / float2::getDistanceSquared( targetParam, sample._parameter );
            const float32        weightSum        = accumWeight + normalizedWeight;
            if ( weightSum < MathUtil::Epsilon )
                continue;

            const DualQuaternion sampleDq = BlendSpaceInternal::splitPose( sample._pose, sampleScale );
            accumDq                       = DualQuaternion::dlb( accumDq, sampleDq, normalizedWeight / weightSum );
            accumScale += sampleScale * normalizedWeight;
            accumWeight = weightSum;
        }

        return BlendSpaceInternal::makePose( accumDq, accumScale );
    }
} // namespace sw
