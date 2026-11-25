/**
 * @file PlayerStateComponent.h
 * @brief 플레이어 한 명의 상태(진행 플래그)를 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class PlayerStateComponent
     * @brief 플레이어 한 명의 진행 플래그를 듭니다. 조종자(로컬 `PlayerControllerComponent` · 서버의 접속마다 세운 원격 조종자)와 같은 오브젝트에 둡니다.
     * @details 월드 상태(`GameStateComponent`)는 씬에 하나 — 멀티플레이에서 "그 플레이어만" 의 기록(도착 컷씬을 본 사람)은 플레이어마다 따로 있어야 한다.
     *          지갑 · 가방 · 퀘스트 일지 · 평판은 같은 오브젝트의 형제 `WalletComponent` · `InventoryComponent` · `QuestLogComponent` · `ReputationComponent`(게임플레이 층)가 든다.
     *          언리얼은 `APlayerState` 를 컨트롤러가 쥐어 폰을 바꿔 타도 남고, 유니티 · Godot 은 게임 코드가 플레이어 객체에 둔다. 여기도 조종자 오브젝트에 둬서 폰을 다시
     *          세워도(부활) 남는다. 폰에서는 `PawnComponent::findControllerObject`(폰 → 쥔 조종자 → 그 오브젝트)로 찾는다.
     *          플래그는 PROPERTY 가 아니라 `ComponentStateCodec` 계약(`serializeState` / `restoreState`)으로 게임 상태 스냅샷에 실린다(판 상태의 플래그와 같은 바이트 형식).
     *          서버의 접속별 영구 저장(계정 진행)은 이 상태를 읽어 쓰는 쪽(온라인 서비스)의 몫이다.
     */
    REFLECT( Category = "Control", DisplayName = "Player State", Tooltip = "One player's state on the controller object (progress flags): kept across pawns, saved with the game state" )
    class SW_GF_API PlayerStateComponent : public Component
    {
    public:
        REFLECT_BODY();
        static constexpr FormatDef kStateFormat{ "PLST" };

        PlayerStateComponent();
        ~PlayerStateComponent() override = default;

        GameFlags&       getProgressFlags() { return _progressFlags; }
        const GameFlags& getProgressFlags() const { return _progressFlags; }

        /** @brief `ComponentStateCodec` 계약입니다 — 판 · 진행 플래그를 씁니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 저장된 바이트로 진행 플래그를 되살립니다. 읽지 못하면 경고를 남기고 지금 값을 둡니다. */
        void restoreState( vector<uint8>&& bytes );

    private:
        SW_CODEC_STATE GameFlags _progressFlags;
    };
} // namespace sw
