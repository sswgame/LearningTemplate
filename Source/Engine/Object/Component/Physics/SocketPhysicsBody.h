/**
 * @file SocketPhysicsBody.h
 * @brief 소켓 부착(`SocketBindingComponent`)이 떼어 낸 단위를 물리에 맡기는 창구입니다. 강체 컴포넌트가 구현합니다(`RigidBodyComponent::getSocketPhysicsBody`).
 * @details 부착은 캐릭터 층(티어 7), 강체는 오브젝트 층(티어 6)이라 창구를 아래층에 둡니다 — 부착 쪽은 이 인터페이스만 압니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @brief 소켓 부착이 물리에 맡기는 창구입니다.
     * @details 떼는 순간 지금 월드 변환 · 속도로 바디를 시작하고, 되돌아가거나 붙을 때 끝냅니다. 그 사이에는 매 틱 바디 변환을 읽어 씁니다.
     */
    class SW_API ISocketPhysicsBody
    {
    public:
        ISocketPhysicsBody()                                       = default;
        virtual ~ISocketPhysicsBody()                              = default;
        ISocketPhysicsBody( const ISocketPhysicsBody& )            = delete;
        ISocketPhysicsBody& operator=( const ISocketPhysicsBody& ) = delete;

        /** @brief 바디를 이 월드 변환 · 선속도로 시작합니다. */
        virtual void beginPhysics( const float4x4& worldTransform, const float3& linearVelocity ) = 0;
        /** @brief 바디를 멈추고 시뮬레이션에서 뺍니다(트랜스폼을 따르게 돌린다). */
        virtual void endPhysics() = 0;
        /** @brief 지금 바디의 월드 변환입니다. 바디가 없으면 false 입니다. */
        virtual bool findBodyWorldTransform( float4x4& outWorldTransform ) const = 0;
    };
} // namespace sw
