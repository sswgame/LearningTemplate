/**
 * @file HorseFollowAIController.h
 * @brief 주인을 따라오는 말 AI입니다. 탄 사람이 없을 때 말을 조종하고, 주인이 멀어지면 다가오며, 휘파람(`whistle`)을 불면 어디서든 옵니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class HorseFollowAIController
     * @brief `AIControllerComponent` 를 상속합니다. 판단만 하고 움직임은 말의 `MountMovementComponent` 가 처리합니다(플레이어가 몰 때와 같은 경로입니다).
     * @details 말 폰의 자동 빙의를 `AI` 로 두고 이 조종자를 가진 프리팹을 주면 시작할 때 말을 조종한다. 누가 타면 빙의가 탄 사람의 조종자로 넘어가고, 내리면
     *          탈 때 말을 조종하던 이 조종자가 다시 조종한다(`MountUtil`).
     */
    REFLECT( Category = "Western", DisplayName = "Horse Follow AI", Tooltip = "Horse AI: follows its owner when nobody rides it, comes at a whistle" )
    class SW_KIT_API HorseFollowAIController : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        HorseFollowAIController();
        ~HorseFollowAIController() override = default;

        /** @brief 주인 오브젝트입니다(따라갈 대상). */
        void                    setOwnerObject( const GameObjectHandle& owner ) { _ownerObject = owner; }
        const GameObjectHandle& getOwnerObject() const { return _ownerObject; }
        /** @brief 휘파람을 붑니다. 거리와 상관없이 주인 곁으로 오며, 닿으면 끝납니다. */
        void whistle() { _bCalled = SW_TRUE; }
        bool isCalled() const { return _bCalled == SW_TRUE; }

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Horse", DisplayName = "Owner", Tooltip = "Object this horse follows" )
        GameObjectHandle _ownerObject;
        PROPERTY( Category = "Horse", DisplayName = "Follow Distance", Min = 0.0, Tooltip = "Starts walking to the owner beyond this distance", Units = m )
        float32 _followDistance;
        PROPERTY( Category = "Horse", DisplayName = "Stop Distance", Min = 0.0, Tooltip = "Stops this close to the owner", Units = m )
        float32 _stopDistance;

        uint8                  _bCalled  : 1; ///< 휘파람을 들었다
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
