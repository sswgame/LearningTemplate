#include "pch.h"

#include "Engine/Animation/Skeletal/PoseInertializer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    SW_LOG_CALLER( "PoseInertializer" );

    namespace
    {
        struct PoseInertializerInternal
        {
            /** @brief ln 2 입니다. 반감기를 감쇠 계수로 바꿀 때 씁니다(Holden 의 `halflife_to_damping` = 4 ln2 / 반감기, 그 절반이 지수의 계수). */
            static constexpr float32 kLn2 = 0.693147181f;

            /** @brief 짧은 쪽(w >= 0)으로 맞춘 정규화 쿼터니언입니다. q 와 -q 는 같은 회전이지만 축-각 벡터는 짧은 쪽이어야 오프셋이 최소입니다. */
            static quaternion makeShortest( const quaternion& rotation )
            {
                const quaternion normalized = rotation.normalize();
                return normalized._w < 0.0f ? -normalized : normalized;
            }

            /** @brief 단위 쿼터니언을 축-각 벡터(축 * 각, 라디안)로 바꿉니다(쿼터니언 로그의 두 배). */
            static float3 makeScaledAngleAxis( const quaternion& rotation )
            {
                const quaternion shortest = makeShortest( rotation );
                const float3     axis{ shortest._x, shortest._y, shortest._z };
                const float32    sinHalf = axis.getLength();
                if ( sinHalf < MathUtil::kEpsilon )
                    return axis * 2.0f;
                const float32 angle = 2.0f * MathUtil::atan2( sinHalf, shortest._w );
                return axis * ( angle / sinHalf );
            }

            /** @brief 축-각 벡터를 단위 쿼터니언으로 바꿉니다(`makeScaledAngleAxis` 의 역). */
            static quaternion makeFromScaledAngleAxis( const float3& scaledAngleAxis )
            {
                const float32 angle = scaledAngleAxis.getLength();
                if ( angle < MathUtil::kEpsilon )
                    return quaternion{ scaledAngleAxis * 0.5f, 1.0f }.normalize();
                const float32 halfAngle = angle * 0.5f;
                return quaternion{ scaledAngleAxis * ( MathUtil::sin( halfAngle ) / angle ), MathUtil::cos( halfAngle ) };
            }

            /** @brief @p from 에서 @p to 로 가는 회전 차이(왼쪽에 곱하는 쪽, `to * inverse( from )`)를 축-각 벡터로 구합니다. */
            static float3 computeRotationDelta( const quaternion& from, const quaternion& to ) { return makeScaledAngleAxis( to * from.inverse() ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PoseInertializer::PoseInertializer()
        : _listTranslationOffset{}
        , _listTranslationVelocity{}
        , _listRotationOffset{}
        , _listRotationVelocity{}
        , _halfLifeSeconds{ 0.0f }
        , _elapsedSeconds{ 0.0f }
        , _bActive{ SW_FALSE }
    {
    }

    bool PoseInertializer::begin( const Pose& sourcePose, const Pose* pSourcePreviousPose, const Pose& targetPose, const Pose* pTargetPreviousPose, float32 deltaSeconds,
                                  float32 halfLifeSeconds )
    {
        reset();
        const uint32 boneCount = targetPose.getBoneCount();
        if ( halfLifeSeconds <= 0.0f )
        {
            SW_LOG_ERROR( "Inertialization half life must be positive (got %#)", halfLifeSeconds );
            return false;
        }
        const bool bSourcePreviousMismatch = pSourcePreviousPose != nullptr && pSourcePreviousPose->getBoneCount() != boneCount;
        const bool bTargetPreviousMismatch = pTargetPreviousPose != nullptr && pTargetPreviousPose->getBoneCount() != boneCount;
        const bool bPreviousMismatch       = bSourcePreviousMismatch || bTargetPreviousMismatch;
        if ( sourcePose.getBoneCount() != boneCount || bPreviousMismatch )
        {
            SW_LOG_ERROR( "Inertialization poses have different bone counts (source %#, target %#)", sourcePose.getBoneCount(), boneCount );
            return false;
        }

        _listTranslationOffset.assign( boneCount, float3{} );
        _listTranslationVelocity.assign( boneCount, float3{} );
        _listRotationOffset.assign( boneCount, float3{} );
        _listRotationVelocity.assign( boneCount, float3{} );
        const bool    bHasDelta      = deltaSeconds > 0.0f;
        const float32 inverseDelta   = bHasDelta ? 1.0f / deltaSeconds : 0.0f;
        const auto&   listSourceMove = sourcePose.getTranslations();
        const auto&   listSourceTurn = sourcePose.getRotations();
        const auto&   listTargetMove = targetPose.getTranslations();
        const auto&   listTargetTurn = targetPose.getRotations();
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            _listTranslationOffset[boneIndex] = listSourceMove[boneIndex] - listTargetMove[boneIndex];
            _listRotationOffset[boneIndex]    = PoseInertializerInternal::computeRotationDelta( listTargetTurn[boneIndex], listSourceTurn[boneIndex] );
            if ( bHasDelta == false )
                continue;
            // 속도 차이 = source 속도 - target 속도. 기록이 없는 쪽의 속도는 0 이다(새 재생의 속도를 모르면 Bollo 원안처럼 source 속도만 이어진다).
            float3 velocity{};
            float3 angularVelocity{};
            if ( pSourcePreviousPose != nullptr )
            {
                velocity        = ( listSourceMove[boneIndex] - pSourcePreviousPose->getTranslations()[boneIndex] ) * inverseDelta;
                angularVelocity = PoseInertializerInternal::computeRotationDelta( pSourcePreviousPose->getRotations()[boneIndex], listSourceTurn[boneIndex] ) * inverseDelta;
            }
            if ( pTargetPreviousPose != nullptr )
            {
                velocity        = velocity - ( listTargetMove[boneIndex] - pTargetPreviousPose->getTranslations()[boneIndex] ) * inverseDelta;
                angularVelocity = angularVelocity - PoseInertializerInternal::computeRotationDelta( pTargetPreviousPose->getRotations()[boneIndex], listTargetTurn[boneIndex] ) * inverseDelta;
            }
            _listTranslationVelocity[boneIndex] = velocity;
            _listRotationVelocity[boneIndex]    = angularVelocity;
        }
        _halfLifeSeconds = halfLifeSeconds;
        _elapsedSeconds  = 0.0f;
        _bActive         = SW_TRUE;
        return true;
    }

    void PoseInertializer::update( float32 deltaSeconds )
    {
        if ( _bActive == SW_FALSE )
            return;
        _elapsedSeconds += MathUtil::max( deltaSeconds, 0.0f );
        if ( _elapsedSeconds >= _halfLifeSeconds * kSettleHalfLifeCount )
            reset();
    }

    void PoseInertializer::apply( Pose& inoutPose ) const
    {
        const uint32 boneCount = getBoneCount();
        if ( _bActive == SW_FALSE || inoutPose.getBoneCount() != boneCount )
            return;
        float3*     pTranslation = inoutPose.getTranslationData();
        quaternion* pRotation    = inoutPose.getRotationData();
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            pTranslation[boneIndex] += computeDecayedOffset( _listTranslationOffset[boneIndex], _listTranslationVelocity[boneIndex] );
            const quaternion offset = PoseInertializerInternal::makeFromScaledAngleAxis( computeDecayedOffset( _listRotationOffset[boneIndex], _listRotationVelocity[boneIndex] ) );
            pRotation[boneIndex]    = ( offset * pRotation[boneIndex] ).normalize();
        }
    }

    void PoseInertializer::reset()
    {
        _listTranslationOffset.clear();
        _listTranslationVelocity.clear();
        _listRotationOffset.clear();
        _listRotationVelocity.clear();
        _halfLifeSeconds = 0.0f;
        _elapsedSeconds  = 0.0f;
        _bActive         = SW_FALSE;
    }

    float3 PoseInertializer::computeTranslationOffset( uint32 boneIndex ) const
    {
        if ( _bActive == SW_FALSE || boneIndex >= getBoneCount() )
            return float3{};
        return computeDecayedOffset( _listTranslationOffset[boneIndex], _listTranslationVelocity[boneIndex] );
    }

    float3 PoseInertializer::computeRotationOffset( uint32 boneIndex ) const
    {
        if ( _bActive == SW_FALSE || boneIndex >= getBoneCount() )
            return float3{};
        return computeDecayedOffset( _listRotationOffset[boneIndex], _listRotationVelocity[boneIndex] );
    }

    float3 PoseInertializer::computeDecayedOffset( const float3& initialOffset, const float3& initialVelocity ) const
    {
        // 임계 감쇠 스프링의 닫힌 식: x(t) = e^(-y t) * ( x0 + ( v0 + y x0 ) t ), y = 2 ln2 / 반감기.
        const float32 damping = 2.0f * PoseInertializerInternal::kLn2 / _halfLifeSeconds;
        const float3  slope   = initialVelocity + initialOffset * damping;
        const float32 decay   = MathUtil::exp( -damping * _elapsedSeconds );
        return ( initialOffset + slope * _elapsedSeconds ) * decay;
    }
} // namespace sw
