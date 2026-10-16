/**
 * @file IPhysicsSoftBodyScene.h
 * @brief 물리 씬의 소프트 바디(천 · 헤어 카드) 쪽 인터페이스입니다. 3D 만 있습니다(차원 트레이트의 `kSupportsSoftBody`).
 * @details 언리얼의 Chaos Cloth, 유니티의 `Cloth` 에 해당한다. 2D 씬은 이 인터페이스를 상속하지 않고 빈 `PhysicsNoSoftBodyScene` 을 상속한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다. 정점은 씬 스텝이 함께 푼다(따로 부를 것이 없다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    struct PhysicsSoftBodyDesc;
} // namespace sw

namespace sw
{
    /** @brief 소프트 바디를 다룹니다. */
    class IPhysicsSoftBodyScene
    {
    public:
        virtual ~IPhysicsSoftBodyScene() = default;

        /** @brief 소프트 바디를 만듭니다. 정점이 없거나 삼각형 인덱스가 셋의 배수가 아니거나 범위를 넘으면 오류를 남기고 무효 핸들을 반환합니다. */
        virtual PhysicsSoftBodyHandle createSoftBody( const PhysicsSoftBodyDesc& desc )       = 0;
        virtual void                  destroySoftBody( PhysicsSoftBodyHandle softBody )       = 0;
        virtual bool                  isSoftBodyValid( PhysicsSoftBodyHandle softBody ) const = 0;
        /** @brief 정점의 지금 월드 위치를 만든 순서대로 채웁니다(비우고 채웁니다). 무효 핸들이면 false 입니다. */
        virtual bool getSoftBodyVertices( PhysicsSoftBodyHandle softBody, vector<float3>& outListPosition ) const = 0;
        /**
         * @brief 고정 정점을 새 자세에 둡니다 — 고정 정점 하나하나가 서술자의 로컬 위치를 이 자세로 옮긴 곳에 놓입니다. 나머지 정점은 구속을 따라 끌려옵니다.
         * @details 천을 단 오브젝트가 움직일 때 프레임마다 부른다(유니티 Cloth 가 트랜스폼을 따르는 것과 같다). 자는 바디는 깨운다.
         */
        virtual void setSoftBodyPinnedTransform( PhysicsSoftBodyHandle softBody, const float3& position, const quaternion& rotation ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 소프트 바디가 없는 차원(2D)의 씬이 `IPhysicsSoftBodyScene` 대신 상속하는 빈 자리입니다. 함수가 없으므로 2D 씬에 부르면 컴파일 오류입니다. */
    struct PhysicsNoSoftBodyScene
    {
    };
} // namespace sw
