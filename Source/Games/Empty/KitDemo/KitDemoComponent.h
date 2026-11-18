/**
 * @file KitDemoComponent.h
 * @brief 장르 키트 시연 가운데 "판을 여는 순간 규칙을 끝까지 돌리고 결과만 쥐는" 것들의 공통 베이스입니다.
 * @details 이 시연들은 모습 · 입력이 없다(언리얼 Functional Test 의 규칙 단계와 같은 자리). 그래서 틱마다 돌릴 규칙도, 세울 오브젝트도 없고
 *          상태는 "마쳤는가" 와 결과 수 몇 개뿐이다. 시연마다 같은 빈 `updateGame` · `onFlush` 와 같은 모양의 `serializeState` 를 적던 것을 여기 한 번 둔다.
 *          모습이 있는 시연(`ActionCombatDemoComponent` · `ActionPlatformerDemoComponent` · `TacticsSRPGDemoComponent`)은 `GameModeComponent` 를 바로 잇는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

#include <initializer_list>

namespace sw
{
    /**
     * @class KitDemoComponent
     * @brief 한 바퀴를 `startGame` 에서 끝내는 키트 시연의 베이스입니다. 파생 클래스는 `startGame` 에서 규칙을 돌리고 `markRunFinished` 를 부르며, 결과 수를 `serializeResults` 로 싣습니다.
     */
    REFLECT( Abstract, Category = "KitDemo", DisplayName = "Kit Demo", Tooltip = "Base of the kit demos that run one whole game when it opens and keep only the result" )
    class KitDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kMaxResultCount = 4; ///< 시연 하나가 상태에 싣는 결과 수의 상한(`serializeResults`)

        KitDemoComponent();
        virtual ~KitDemoComponent() override;

        /** @brief 자동화 감시 값이 읽는 시연 상태입니다(0 시작 전, 1 한 바퀴를 마침). */
        int32 getDemoState() const { return _demoState; }

    protected:
        /** @brief 한 바퀴는 판을 열 때 끝났다 — 틱마다 돌릴 규칙이 없습니다. */
        void updateGame( float32 deltaTime ) final;
        /** @brief 모습이 없어 세울 오브젝트가 없습니다. */
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) final;

        /** @brief 한 바퀴를 마쳤다고 적습니다(`getDemoState` 가 1). 결과를 모두 담은 뒤에 부릅니다. */
        void markRunFinished() { _demoState = 1; }
        /**
         * @brief 시연 상태와 결과 수(@p listResult, 적은 순서가 바이트 순서)를 표준 머리 뒤에 놓습니다. 파생 클래스의 `serializeState` 몸체입니다.
         * @details 읽기는 지역 사본에 받아 끝까지 맞을 때만 대입한다 — 반쯤 읽힌 결과를 남기지 않는다.
         */
        void serializeResults( StateIO& io, uint32 stateTag, uint32 stateVersion, std::initializer_list<int32*> listResult );

    private:
        int32 _demoState; ///< 0 시작 전, 1 한 바퀴를 마침
    };
} // namespace sw
