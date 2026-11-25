/**
 * @file GameFramework/Client/Flow/TitleMenuComponent.h
 * @brief 타이틀 씬의 오브젝트에 두면 플레이하는 동안 엔진 메인 메뉴(`MainMenuScreen`)를 띄우는 컴포넌트를 정의합니다.
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UIScreenTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class UISystem;

    /**
     * @class TitleMenuComponent
     * @brief 플레이 시작에 메인 메뉴를 열고 끝에 닫습니다(Lyra 의 Frontend 상태 컴포넌트가 메인 메뉴를 밀어 넣는 것, 유니티 메뉴 씬의 Canvas).
     * @details 문서는 엔진 기본 에셋 `_uiMainMenu` 하나다(`UISystem::getEngineDocuments()._mainMenu`). "시작" 이 다음 씬을 열면 이 씬이 내려가며 메뉴도 닫힌다.
     *          UI 시스템이 없으면(전용 서버, 헤드리스 테스트) 아무것도 띄우지 않는다. 게임 스레드에서만 쓴다.
     */
    REFLECT( Category = "UI", DisplayName = "Title Menu", Tooltip = "Opens the engine main menu while this object plays (put it in the title scene)" )
    class SW_GF_API TitleMenuComponent : public Component
    {
    public:
        REFLECT_BODY();

        TitleMenuComponent();
        ~TitleMenuComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;

    private:
        void closeMenu();

    private:
        UISystem*                   _pUISystem; ///< 메뉴를 연 UI 시스템(없으면 nullptr)
        SW_TRANSIENT UIScreenHandle _menu;
    };
} // namespace sw
