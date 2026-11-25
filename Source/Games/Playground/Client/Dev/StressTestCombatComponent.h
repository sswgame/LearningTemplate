/**
 * @file StressTestCombatComponent.h
 * @brief `-gv_stressTestCombat=1` 의 연출 — KayKit 스켈레톤 적이 걸으며(발소리 알림) 무기 레이캐스트를 맞고(히트 존 · 움찔), 치명적 맞음에 래그돌로 쓰러지고,
 *        오른손 소켓의 칼이 물리로 떨어지는 장면을 시간표대로 돌립니다(게임플레이 · 애니메이션 · 물리 이음의 눈 검증).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct StressTestSceneSettings;

    class GameObject;
    class GameObjectManager;
    class Scene;

    /**
     * @class StressTestCombatComponent
     * @brief 정해진 프레임 순서로 전투 연출을 진행합니다. 실행마다 같은 프레임에 같은 일이 일어나도록 순서를 고정했습니다.
     *        60프레임에 머리를 맞아 움찔하고, 120프레임에 가슴을 맞아 움찔합니다.
     *        180프레임에 가슴을 치명적으로 맞아 래그돌이 되고 칼이 손에서 떨어집니다. 900프레임에 가라앉아 있으면 일어납니다.
     *        칼이 붙어 있는 동안에는 프레임마다 손 소켓을 따라가도록 소켓 변환을 넘깁니다.
     *        맞은 횟수와 발소리 수는 로그(`[StressTestCombat]`)로 남깁니다.
     */
    REFLECT()
    class StressTestCombatComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 애니메이션과 물리가 끝난 뒤에 실행합니다. 이번 프레임의 손 위치로 칼을 맞추고, 이번 프레임에 울린 알림을 셉니다.

        StressTestCombatComponent();
        virtual ~StressTestCombatComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 래그돌을 가진 걷는 스켈레톤 적 하나를 @p position 에 스폰합니다. `-gv_stressTestRagdolls` 군중도 이 함수를 씁니다. 에셋 경로는 @p settings 의 `_combat*` 값입니다. */
        static GameObject* spawnEnemy( GameObjectManager& objects, const StressTestSceneSettings& settings, const hashed_string& name, const float3& position );

        /** @brief 바닥, 적, 칼, 연출 오브젝트를 씬에 스폰합니다. 에셋 경로의 원본은 벤치 설정(`benchscene.xml` 의 `_combat*`)입니다. 스폰한 오브젝트의 핸들은 @p outListObject 뒤에 추가합니다. */
        static void spawnScene( Scene& scene, const StressTestSceneSettings& settings, vector<GameObjectHandle>& outListObject );

    private:
        /** @brief 적의 오른손 소켓 변환(적 루트 기준)을 칼의 부착 정보에 넘깁니다. */
        void followHandSocket( GameObjectManager& manager, GameObject& enemy, GameObject& sword ) const;
        /** @brief 정면에서 적의 뼈 높이로 무기를 한 발 쏩니다. @p bFatal 이 true 이면 치명적인 피격으로 알립니다. */
        void shootAt( GameObjectManager& manager, GameObject& enemy, const utf8* pBone, bool bFatal );
        /** @brief 적의 마지막 알림 처리 기록에서 발소리를 세고 남깁니다. 틱 뒤(게임 스레드)에 부른다 — 기록은 적의 것이다. */
        void logFootstep( const GameObject& enemy );
        /** @brief 초가 바뀌면 발소리 수 · 래그돌 상태 · 칼 자리를 한 줄 남깁니다. 틱 뒤(게임 스레드)에 부른다. */
        void logEverySecond( const GameObject& enemy, const GameObject& sword );

        SW_TRANSIENT GameObjectHandle _enemy;
        SW_TRANSIENT GameObjectHandle _sword;
        SW_TRANSIENT float32          _elapsed;
        SW_TRANSIENT uint32           _frame;
        SW_TRANSIENT uint32           _stage;
        SW_TRANSIENT uint32           _footstepCount;
        SW_TRANSIENT uint32           _loggedSecond;
        SW_TRANSIENT float32          _shooterRange; ///< 적 앞 몇 m 에서 쏘는지입니다. 원본은 벤치 설정의 `_combatShooterRange` 이고 `spawnScene` 이 넣어 줍니다.
    };
} // namespace sw
