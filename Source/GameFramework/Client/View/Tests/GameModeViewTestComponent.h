/**
 * @file GameModeViewTestComponent.h
 * @brief 디렉터 뷰의 공통 몫(`GameModeViewComponent`) 테스트가 다는 뷰입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/GameModeTestComponent.h"

namespace sw
{
    /** @brief 갱신이 온 수와 그때의 라운드를 적는 뷰입니다. */
    REFLECT( HideInMenu )
    struct GameModeViewTestComponent : public GameModeViewComponent
    {
        REFLECT_BODY();

        GameModeViewTestComponent()
            : GameModeViewComponent( GameModeViewBinding::of<&GameModeViewTestComponent::updateView>() )
            , _updateCount{ 0 }
            , _seenRound{ 0 }
        {
        }

        void updateView( GameObjectManager& manager, GameModeTestComponent& gameMode )
        {
            (void)manager;
            ++_updateCount;
            _seenRound = gameMode._roundCount;
        }

        uint32 _updateCount;
        int32  _seenRound;
    };
} // namespace sw
