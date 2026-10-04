/**
 * @file BenchCombatComponent.h
 * @brief `-gv_benchCombat=1` 의 연출 — KayKit 스켈레톤 적이 걸으며(발소리 알림) 무기 레이캐스트를 맞고(히트 존 · 움찔), 치명적 맞음에 래그돌로 쓰러지고,
 *        오른손 소켓의 칼이 물리로 떨어지는 장면을 시간표대로 돌립니다(게임플레이 · 애니메이션 · 물리 이음의 눈 검증).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class Scene;

    /**
     * @class BenchCombatComponent
     * @brief 시간표(프레임 — 실행마다 같은 순서로 찍히게): 60 머리 맞음(움찔) → 120 가슴 맞음(움찔) → 180 치명적 가슴 맞음(래그돌 · 칼 떼기) → 900 가라앉았으면 기상.
     *        칼이 붙어 있는 동안은 프레임마다 손 소켓을 따라
     *        소켓 변환을 넘깁니다. 맞음 · 발소리 수는 로그(`[BenchCombat]`)로 남깁니다.
     */
    REFLECT()
    class BenchCombatComponent : public Component
    {
    public:
        REFLECT_BODY();

        BenchCombatComponent();
        virtual ~BenchCombatComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 바닥 · 적 · 칼 · 연출 오브젝트를 씬에 세웁니다. 만든 오브젝트들의 핸들을 @p outListObject 에 붙입니다. */
        static void spawnScene( Scene& scene, vector<GameObjectHandle>& outListObject );

    private:
        /** @brief 적의 오른손 소켓 변환(적 루트 기준)을 칼의 부착에 넘깁니다. */
        void followHandSocket( GameObjectManager& manager, GameObject& enemy, GameObject& sword ) const;
        /** @brief 정면에서 적의 뼈 높이로 무기 한 발을 쏩니다. @p bFatal 이면 맞음을 치명으로 알립니다. */
        void shootAt( GameObjectManager& manager, GameObject& enemy, const utf8* pBone, bool bFatal );

        GameObjectHandle _enemy;
        GameObjectHandle _sword;
        float32          _elapsed;
        uint32           _frame;
        uint32           _stage;
        uint32           _footstepCount;
        uint32           _loggedSecond;
    };
} // namespace sw
