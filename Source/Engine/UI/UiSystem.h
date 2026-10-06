/**
 * @file UiSystem.h
 * @brief 런타임 UI 의 엔진 서비스입니다 — 화면 스택 · 입력 → 사건 · 갱신 순서를 돕니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class FontSystem;
    class InputManager;

    /**
     * @class UiSystem
     * @brief 런타임 UI 의 엔진 서비스입니다(`engine::getUiSystem()` · 게임은 `game::getService<UiSystem>()`).
     * @details 언리얼 `FSlateApplication` + CommonUI `UCommonUIActionRouter` 의 자리입니다. 틱은 둘로 나뉩니다 —
     *          `processInput`(입력 갱신 직후, 게임 틱 **앞** — UI 가 먹은 입력을 폰이 못 보게)과 `update`(게임 틱 **뒤**, 렌더 패킷 앞 —
     *          이번 프레임의 게임 상태로 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기). 기동 단계 `Ui`(Client 대상)가 `initialize` 합니다.
     *          게임 스레드만.
     */
    class SW_API UiSystem
    {
    public:
        UiSystem();
        ~UiSystem();
        UiSystem( const UiSystem& )            = delete;
        UiSystem& operator=( const UiSystem& ) = delete;

        /**
         * @brief 입력을 묶고 일을 시작합니다.
         * @param inputManager 행동 · 포인터를 읽는 입력(시험은 자기 것을 넘긴다).
         * @param pFontSystem 글 측정 · 그리기가 쓰는 글꼴(레이아웃부터). 없으면 글 위젯이 크기 0 입니다.
         */
        [[nodiscard]] bool initialize( InputManager& inputManager, FontSystem* pFontSystem );
        void               shutdown();
        bool               isInitialized() const { return _pInput != nullptr; }

        /** @brief 입력 → UI 사건(게임 틱 앞). */
        void processInput( float32 deltaSeconds );
        /** @brief 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기(게임 틱 뒤). @p viewport 는 이번 프레임에 UI 를 그릴 화면입니다. */
        void update( float32 deltaSeconds, const UiViewport& viewport );

        InputManager*     getInputManager() const { return _pInput; }
        FontSystem*       getFontSystem() const { return _pFontSystem; }
        const UiViewport& getViewport() const { return _viewport; }

    private:
        InputManager* _pInput;
        FontSystem*   _pFontSystem;
        UiViewport    _viewport;
    };
} // namespace sw
