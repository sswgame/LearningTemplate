/**
 * @file GimmickUtil.h
 * @brief 기믹 컴포넌트가 함께 쓰는 주인 없는 도구입니다. 오브젝트의 몸(콜라이더와 그래픽) · 상호작용을 켜고 끄고, 아이템을 주고, 연출 신호를 냅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;
    class GameObject;

    /** @struct GimmickUtil */
    struct SW_GF_API GimmickUtil
    {
        /**
         * @brief 오브젝트의 몸(기믹 논리 컴포넌트와 @p pKeep 을 뺀 모든 컴포넌트)을 켜고 끕니다. 무너진 발판이나 먹은 아이템 상자를 숨기고 복원할 때 씁니다.
         * @details 꺼진 콜라이더는 겹침에 포함되지 않고, 꺼진 그래픽은 그려지지 않는다. 틱 중이면 틱 뒤로 미룬다.
         */
        static void setBodyActive( GameObject& object, bool bActive, const Component* pKeep );
        /** @brief 오브젝트의 `InteractableComponent` 를 켜고 끕니다. 다 쓴 채집 지점에 씁니다. 상호작용하는 쪽이 병렬 틱에서 읽으므로 틱 중이면 틱 뒤로 미룹니다. */
        static void setInteractableEnabled( GameObject& object, bool bEnabled );
        /**
         * @brief 폰 @p target 을 쥔 플레이어의 가방(`PlayerProgressComponent::findInventory`)에 @p item × @p count 를 넣습니다. 아이템 상자 · 채집 지점이 씁니다.
         * @details 받는 쪽이 그 가방 하나라 이벤트를 거치지 않고 직접 넣는다(언리얼 Lyra 의 픽업이 `ULyraInventoryManagerComponent::AddItemDefinition` 을 직접 부르는 자리).
         *          가방은 다른 오브젝트(조종자)의 상태라 틱 중이면 틱 뒤로 미룬다. 폰 · 조종자 · 가방이 없거나 가방이 차면 못 넣은 만큼 주지 않는다.
         */
        static void giveItem( const GameObject& target, const hashed_string& item, int32 count );
        /**
         * @brief @p source 에서 연출 신호 @p cue 를 냅니다. 공포 트리거 · 폭발 · 파괴 단계가 씁니다. @p target 은 신호를 부른 쪽(트리거에 들어온 몸)이고 없으면 빈 핸들입니다.
         * @details 연출(프리팹 · 소리)은 어빌리티 게임플레이 큐와 같은 표(카탈로그 `<AbilityPresentation tag="<큐 이름>">`)와 클라이언트 창구(`IAbilityCueSink`)로 바로 낸다 —
         *          언리얼 GameplayCueManager 가 GAS 밖 큐(`ExecuteGameplayCue_NonReplicated`)도 같은 표로 받는 자리. 표에 없는 큐는 연출 없이 이벤트만 나간다.
         *          받는 쪽이 여럿일 수 있어(게임 규칙 · 시퀀서 · 퀘스트) `GimmickCueEvent` 도 "game" 채널에 낸다(`AbilitySystemComponent` 의 큐와 같은 두 길).
         */
        static void sendCue( const GameObject& source, const hashed_string& cue, const float3& position, float32 magnitude, GameObjectHandle target = GameObjectHandle{} );
    };
} // namespace sw
