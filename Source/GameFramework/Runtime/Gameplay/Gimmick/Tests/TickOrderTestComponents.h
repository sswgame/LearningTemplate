/**
 * @file TickOrderTestComponents.h
 * @brief 틱 순서 단위 테스트(`TestTickedPathMover.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"

namespace sw
{
    /**
     * @brief 넷 키트처럼 자기 주 틱에서 씬에 서버 틱을 알리는 컴포넌트입니다. 알리는 차이가 프레임마다 1 씩 늘어 한 프레임 늦게 읽으면 값이 어긋난다.
     * @details 시작할 때 자기 주 틱을 서버 틱 알림 틱으로 건다(`ControlSystem::setServerTickSource`) — 바닥 서브틱이 그 뒤에 돈다.
     */
    REFLECT( HideInMenu )
    struct ServerTickSourceTestComponent : public Component
    {
        REFLECT_BODY();

        uint32 _lastServerTick = 0; ///< 이번 프레임에 알린 서버 틱
        int64  _offset         = 100;

        void onBeginPlay() override
        {
            Component::onBeginPlay();
            ControlSystem::findOrCreate( *getOwner()->getManager() ).setServerTickSource( getTickHandle() );
        }

        void onTick( TickContext& context ) override
        {
            Component::onTick( context );
            ControlSystem* pControl = ControlSystem::find( *getOwner()->getManager() );
            if ( pControl == nullptr )
                return;
            ++_offset;
            const int64 serverTick = static_cast<int64>( pControl->getTick() ) + _offset;
            pControl->publishServerTick( serverTick );
            _lastServerTick = static_cast<uint32>( serverTick );
        }
    };
} // namespace sw
