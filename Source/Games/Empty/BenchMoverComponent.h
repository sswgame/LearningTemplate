/**
 * @file BenchMoverComponent.h
 * @brief 벤치 큐브가 **틱 안에서** 자기 위치를 쓰게 하는 컴포넌트 — `-gv_benchTickMovers=N` 으로 붙는다.
 *
 * @details 게임플레이의 보통 모양은 `onTick` 안에서 자기 트랜스폼을 바꾸는 것이다(`GravityComponent` ·
 *          `ProjectileComponent`). 그런데 기본 벤치는 게임 스레드가 틱 **밖에서** 배치로 쓰므로 그 경로를
 *          한 번도 재지 못했다 — 병렬 틱 중의 세터는 지연 경로를 타고, 그 비용은 프로파일에 없었다.
 *          이 컴포넌트가 그 경로를 태운다. `BenchScene.*` 와 함께 새 게임을 시작할 때 지울 파일이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SceneComponent;

    /**
     * @class BenchMoverComponent
     * @brief 틱마다 같은 오브젝트의 씬 컴포넌트에 위치·스케일 세터를 부릅니다.
     */
    REFLECT()
    class BenchMoverComponent : public Component
    {
    public:
        REFLECT_BODY();

        BenchMoverComponent();
        virtual ~BenchMoverComponent() override = default;

        /** @brief 움직일 대상과 격자 자리(기준 위치)·위상을 정합니다. 쓰지 않는 무버는 틱만 돈다. */
        void setTarget( SceneComponent* pTarget, const float3& basePosition, float32 phase, bool bWritesTransform );

        void onTick( float32 deltaTime ) override;

    private:
        SceneComponent* _pTarget;
        float3          _basePosition;
        float32         _phase;
        float32         _elapsed;
        uint8           _bWritesTransform : 1;
        uint8           _reserved         : 7;
    };
} // namespace sw
