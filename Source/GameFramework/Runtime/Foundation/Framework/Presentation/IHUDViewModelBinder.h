/**
 * @file GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h
 * @brief 게임이 든 HUD 뷰모델을 오브젝트의 HUD 에 거는 창구입니다 — 게임 코드는 HUD 컴포넌트(UI)를 모르고 뷰모델만 들고 값을 넣습니다.
 * @details 언리얼에서 게임 코드가 값을 폰 · 플레이어 상태에 두고 HUD(클라이언트에만 있다)가 읽는 갈림과 같은 자리다(기억). 구현은 클라이언트 전용 소스
 *          (`GameFramework/Client/HUD`)의 모듈 서브시스템이고, 전용 서버에는 없어 `game::findSubsystem` 이 nullptr 이다 — 값은 넣어도 보일 곳이 없다.
 *          뷰모델은 게임이 소유한다. HUD 는 빌려 쓰고, 뷰모델이 먼저 지워지면 스스로 뗀다(`IUIViewModelObserver`).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObject;
    class UIViewModel;

    /** @class IHUDViewModelBinder @brief HUD 뷰모델을 거는 쪽입니다. 게임 스레드에서 부릅니다. */
    class IHUDViewModelBinder
    {
    public:
        virtual ~IHUDViewModelBinder() = default;

        /**
         * @brief @p owner 의 HUD 에 @p pViewModel 을 겁니다(nullptr 이면 뗍니다). 같은 뷰모델이면 아무 일도 하지 않습니다.
         * @return 오브젝트에 HUD 가 없거나 다른 뷰모델이 이미 걸려 있으면 false 입니다(오류는 HUD 가 남긴다).
         */
        virtual bool bindHUDViewModel( GameObject& owner, UIViewModel* pViewModel ) = 0;
    };
} // namespace sw
