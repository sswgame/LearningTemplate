/**
 * @file UIOwnedScreen.h
 * @brief 연 쪽이 닫아야 하는 화면 하나를 소유하는 이동 전용 값입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"

#include "Engine/UI/Screen/UIScreenTypes.h"

namespace sw
{
    class UIScreen;
    class UISystem;

    /**
     * @class UIOwnedScreen
     * @brief 화면 하나를 소유합니다. 소멸 · `reset` · 이동 대입이 그 화면을 닫습니다(지연 — 닫기 애니메이션이 있으면 재생하고 지운다).
     * @details 밖에서 화면을 닫는 길은 이것과 화면 자신(`UIScreen::close`)뿐이다 — 핸들을 쥐고 닫기를 잊을 수 없다. 사람이 먼저 닫은 화면과 먼저 내려간 UI 시스템에는 아무것도 하지 않는다.
     *          스스로 닫히는 화면(확인 창 · 메뉴)은 소유하지 않고 핸들만 쓴다.
     */
    class SW_API UIOwnedScreen
    {
    public:
        /** @brief 아무것도 소유하지 않습니다. */
        UIOwnedScreen() noexcept;
        /** @brief @p ui 가 연 화면 @p handle 을 소유합니다. 열지 못한 핸들이면 빈 값입니다. */
        UIOwnedScreen( UISystem& ui, UIScreenHandle handle );
        ~UIOwnedScreen();

        UIOwnedScreen( const UIOwnedScreen& )            = delete;
        UIOwnedScreen& operator=( const UIOwnedScreen& ) = delete;
        UIOwnedScreen( UIOwnedScreen&& other ) noexcept;
        UIOwnedScreen& operator=( UIOwnedScreen&& other ) noexcept;

        /** @brief 소유한 화면을 닫고 비웁니다. */
        void reset() noexcept;
        /** @brief 화면을 소유하고 있으면 true 입니다(사람이 닫은 뒤에도 `reset` 까지 true 다 — 떠 있는지는 `find` 로 본다). */
        bool isHeld() const noexcept { return _handle != kInvalidUIScreenHandle; }
        /** @brief 소유한 화면의 핸들입니다. 빈 값이면 `kInvalidUIScreenHandle` 입니다. */
        UIScreenHandle getHandle() const noexcept { return _handle; }
        /** @brief 떠 있는(닫히는 중이 아닌) 소유 화면입니다. 없으면 nullptr 입니다. */
        UIScreen* find() const;

    private:
        Subscription   _closeSubscription; ///< 풀리면 UI 시스템이 화면을 닫는다(`UISystem::ownScreen`)
        UISystem*      _pUISystem;         ///< 화면을 연 UI 시스템. `_closeSubscription` 이 살아 있는 동안만 본다
        UIScreenHandle _handle;
    };
} // namespace sw
