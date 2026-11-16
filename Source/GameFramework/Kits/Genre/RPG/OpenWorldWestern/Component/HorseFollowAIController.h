/**
 * @file HorseFollowAIController.h
 * @brief 주인을 따라오는 말 AI입니다. 탄 사람이 없을 때 말을 조종합니다. 감각(주인까지 거리 · 따라갈 자리)과 휘파람(`whistle`)만 내고, 판단은 행동 트리 데이터입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class HorseFollowAIController
     * @brief `AIControllerComponent` 를 상속합니다. 움직임은 말의 `MountMovementComponent` 가 처리합니다(플레이어가 몰 때와 같은 경로입니다).
     * @details 말 폰의 자동 빙의를 `AI` 로 두고 이 조종자를 가진 프리팹을 주면 시작할 때 말을 조종한다. 누가 타면 빙의가 탄 사람의 조종자로 넘어가고, 내리면
     *          탈 때 말을 조종하던 이 조종자가 다시 조종한다(`VehicleSeatComponent`).
     *          블랙보드: `FollowPosition`(주인 자리, 높이는 말의 것) · `OwnerDistance`(수평 m) — 주인이나 말의 자리가 없으면 지운다. `Called`(휘파람을 들었다 — 트리가 닿으면 지운다).
     *          판단(멀어지면 다가오고, 가까우면 멈추고, 부르면 어디서든 온다)과 거리는 트리 데이터(게임 팩의 `horse.bt.xml`)가 정한다.
     */
    REFLECT( Category = "Western", DisplayName = "Horse Follow AI", Tooltip = "Horse AI: senses its owner when nobody rides it; its behavior tree decides when to follow" )
    class SW_KIT_API HorseFollowAIController : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        HorseFollowAIController();
        ~HorseFollowAIController() override = default;

        /** @brief 주인 오브젝트입니다(따라갈 대상). */
        void                    setOwnerObject( const GameObjectHandle& owner ) { _ownerObject = owner; }
        const GameObjectHandle& getOwnerObject() const { return _ownerObject; }
        /** @brief 휘파람을 붑니다(블랙보드 `Called`). 트리가 거리와 상관없이 주인 곁으로 오게 하고, 닿으면 지웁니다. */
        void whistle();
        bool isCalled() const;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Horse", DisplayName = "Owner", Tooltip = "Object this horse follows" )
        GameObjectHandle _ownerObject;
    };
} // namespace sw
