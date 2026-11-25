/**
 * @file GimmickUtil.h
 * @brief 기믹 컴포넌트가 함께 쓰는 주인 없는 도구입니다. 오브젝트의 몸(콜라이더와 그래픽) · 상호작용을 켜고 끄고, 아이템을 주고, 연출 신호를 내고, 피해를 줍니다.
 * @details 피해는 단일 경로다. 대상이 기믹 센서를 가지고 있으면 그 센서의 Damage 입력에 넣고(폭발 연쇄가 쓴다), 체력은 엔진 맞음 길
 *          (`CharacterRayHit::deliverHit` → `GameObjectEvents::_hitReceived`, `HitInfo::_kind` = 피해 종류)로 깎는다.
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
    class FractureBaseComponent;
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
         * @brief @p target 이 받을 가방(`IItemReceiver::findForTarget` — 자신 또는 폰을 쥔 조종자)에 @p item × @p count 를 모두 넣습니다. 아이템 상자 · 채집 지점이 씁니다.
         * @details 받는 쪽이 그 가방 하나라 이벤트를 거치지 않고 계약으로 직접 넣는다(언리얼 Lyra 의 픽업이 `AddItemDefinition` 을 직접 부르는 자리). 가방은 다른 오브젝트의 상태라
         *          게임 스레드(틱 밖)에서 부른다. 받는 쪽이 없거나 거절하면(가방 없음 · 자리 없음) 아무것도 넣지 않고 경고를 남긴 뒤 false — 주는 쪽은 아이템을 소비하지 않는다.
         */
        [[nodiscard]] static bool giveItem( const GameObject& giver, const GameObject& target, const hashed_string& item, int32 count );
        /**
         * @brief @p source 에서 연출 신호 @p cue 를 냅니다. 공포 트리거 · 폭발 · 파괴 단계가 씁니다. @p target 은 신호를 부른 쪽(트리거에 들어온 몸)이고 없으면 빈 핸들입니다.
         * @details 연출(프리팹 · 소리 · 카메라 흔들림)은 어빌리티 게임플레이 큐와 같은 표(카탈로그 `<AbilityPresentation tag="<큐 이름>">`)와 클라이언트 창구(`IAbilityCueSink`)로 바로 낸다 —
         *          언리얼 GameplayCueManager 가 GAS 밖 큐(`ExecuteGameplayCue_NonReplicated`)도 같은 표로 받는 자리. 표에 없는 큐는 연출 없이 이벤트만 나간다.
         *          받는 쪽이 여럿일 수 있어(게임 규칙 · 시퀀서 · 퀘스트) `GimmickCueEvent` 도 "game" 채널에 낸다(퀘스트 일지 `QuestLogComponent` 가 받는다).
         */
        static void sendCue( const GameObject& source, const hashed_string& cue, const float3& position, float32 magnitude, GameObjectHandle target = GameObjectHandle{} );
        /**
         * @brief @p target 에 @p amount 만큼 피해를 줍니다. 아무 스레드에서 불러도 됩니다.
         * @details 틱 중이면 틱 뒤의 게임 스레드로 미룬다. 병렬 틱에서 다른 오브젝트의 센서에 바로 넣으면, 그 센서가 이번 틱에 처리할지 다음 틱에 처리할지가
         *          스케줄에 달려서 결과가 결정적이지 않다. 연쇄 폭발과 롤백에서 문제가 된다. 미루면 항상 다음 틱에 처리한다.
         */
        static void applyDamage( const GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount );
        /** @brief 파괴 컴포넌트를 그 경계 구 크기의 폭발로 중심에서 깹니다. 조각을 깎아 내거나 통째로 부술 때 씁니다(폭발 드럼통 · 부서지는 엄폐물). */
        static void breakAtCenter( FractureBaseComponent& fracture, const float3& center, float32 strain, float32 impulse );
    };
} // namespace sw
