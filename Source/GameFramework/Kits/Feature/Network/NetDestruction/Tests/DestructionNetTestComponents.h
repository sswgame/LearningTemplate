/**
 * @file DestructionNetTestComponents.h
 * @brief 파괴 세션 단위 테스트(`TestDestructionNetSessionComponent.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

namespace sw
{
    /**
     * @brief 같은 틱 그룹(`PostUpdate`)에서 파괴 세션 컴포넌트의 주 틱 뒤에 도는 서브틱으로 세션의 서버 틱 번호를 읽어 둡니다.
     * @details 세션이 틱 안에서 복제를 돌리면 이 서브틱은 이번 프레임에 오른 번호를 보고, 틱 뒤 큐에서 돌리면 아직 오르기 전 번호를 본다 — 둘을 가르는 관측점이다.
     *          선행 조건은 첫 서브틱에서 걸고 다음 프레임부터 든다(틱 중 바꿈은 틱 뒤에 든다).
     */
    REFLECT( HideInMenu )
    struct DestructionTickReaderTestComponent : public Component
    {
        REFLECT_BODY();

        static constexpr uint32 kReadSubTickID = 1;

        const DestructionNetSessionComponent* _pSession       = nullptr; ///< 테스트가 넣는다(같은 매니저의 오브젝트)
        uint32                                _seenServerTick = 0;       ///< 이번 프레임 서브틱이 본 세션의 서버 틱 번호
        uint8                                 _bBound         = SW_FALSE;

        void onBeginPlay() override
        {
            Component::onBeginPlay();
            registerSubTick( TickGroup::PostUpdate, kReadSubTickID );
        }

        void onSubTick( uint32 subTickID, TickContext& context ) override
        {
            Component::onSubTick( subTickID, context );
            if ( subTickID != kReadSubTickID || _pSession == nullptr )
                return;
            if ( _bBound == SW_FALSE )
            {
                _bBound = SW_TRUE;
                (void)addSubTickPrerequisite( kReadSubTickID, _pSession->getTickHandle(), TickPrerequisiteKind::OrderOnly );
            }
            _seenServerTick = _pSession->getSession().getServerTick();
        }
    };
} // namespace sw
