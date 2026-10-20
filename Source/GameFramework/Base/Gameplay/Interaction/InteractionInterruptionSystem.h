/**
 * @file InteractionInterruptionSystem.h
 * @brief 상호작용(말 걸기)이 NPC 의 하루 일정에 끼어들게 합니다. 플레이어가 NPC 와 상호작용하는 동안 그 NPC 에 일정 끼어들기(`pushInterruption`, 기본 "Talk")를 쌓고, 끝나면 뺍니다.
 * @details 상호작용 쪽은 일정을 모르고(`InteractorComponent` 는 끼어들기를 모른다), 이 시스템이 매 프레임 게임 스레드에서 상호작용 중인 대상을 읽어 일정에 알린다.
 *          일정은 끼어들기가 모두 빠지면 **지금 시각의 블록**으로 돌아가므로 말을 걸어 시간이 흘렀어도 늦은 채로 이어지지 않는다. 끼어든 동안 NPC 의 몸(걷기 멈춤)은 게임이 `Interrupted` 사건으로 처리한다.
 *          어떤 오브젝트가 어느 NPC 인지는 기본으로 **오브젝트 이름 = NPC id** 이고, 다르게 이어야 하면 `setNpcResolver` 로 바꾼다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class ScheduleSystem;

    /**
     * @class InteractionInterruptionSystem
     * @brief 상호작용 중인 대상 NPC 에 일정 끼어들기를 쌓고 뺍니다. 게임 스레드에서 틱 뒤에 한 번 부릅니다.
     */
    class SW_GF_API InteractionInterruptionSystem
    {
    public:
        /** @brief 오브젝트가 어느 NPC 인지 답하는 함수입니다. NPC 가 아니면 -1 을 돌려줍니다. */
        using NpcResolveFunc = int32 ( * )( const ScheduleSystem& schedule, const GameObject& target );

        InteractionInterruptionSystem();

        /**
         * @brief 일정 시스템과 이름을 정합니다.
         * @param interactionID 이 상호작용 정의(`InteractionDef::_id`)만 끼어들기로 칩니다. 비어 있으면 모든 상호작용입니다.
         * @param interruptID 일정 카탈로그의 `<Interrupt id>` 입니다(기본 "Talk").
         */
        void initialize( ScheduleSystem* pSchedule, const hashed_string& interactionID = hashed_string{}, const hashed_string& interruptID = hashed_string( "Talk" ) );
        /** @brief 쌓은 끼어들기를 모두 빼고 비웁니다. */
        void clear();
        /** @brief 오브젝트 → NPC 번호 함수를 바꿉니다(nullptr 이면 오브젝트 이름 = NPC id). */
        void setNpcResolver( NpcResolveFunc pResolver ) { _pResolver = pResolver; }

        /**
         * @brief 씬의 상호작용하는 쪽(`InteractorComponent`)을 읽어 대상 NPC 의 끼어들기를 맞춥니다. 상호작용을 시작한 대상은 쌓고, 끝난 대상은 뺍니다.
         * @details 씬의 상호작용하는 쪽을 훑으므로 프레임에 한 번만 부릅니다.
         */
        void update( GameObjectManager& manager );
        /**
         * @brief 지금 말을 거는 중인 대상 목록으로 끼어들기를 맞춥니다(`update` 가 모은 목록을 넘기는 함수이며, 씬 없이 테스트할 수 있다).
         * @param listTarget 지금 상호작용을 받고 있는 오브젝트들입니다(중복 가능).
         */
        void applyTargets( const vector<const GameObject*>& listTarget );

        /** @brief 지금 끼어들기를 쌓아 둔 NPC 수입니다. */
        size_t getActiveCount() const { return _listActive.size(); }

    private:
        /** @brief 쌓아 둔 끼어들기 하나입니다. */
        struct Active
        {
            uint64 _targetObjectID{ 0 };
            int32  _npcIndex{ -1 };
        };

        int32 resolveNpc( const GameObject& target ) const;

        ScheduleSystem* _pSchedule;
        NpcResolveFunc  _pResolver;
        hashed_string   _interactionID;
        hashed_string   _interruptID;
        vector<Active>  _listActive;
    };
} // namespace sw
