/**
 * @file MainWindowSubsystem.h
 * @brief 엔진 범위의 주 창 서비스입니다 — 주 창을 소유하고, 입력 · RHI · 자동화 · 에디터가 `engine::findSubsystem<MainWindowSubsystem>()` 로 찾아 읽습니다.
 * @details 예전에는 `IWindow::setActiveWindow` / `getActiveWindow` 정적 포인터가 두 일을 함께 했다: Engine 이 만든 창을 App 에 넘기는 소유권 전달 칸(`release()` 로 놓고 App 이
 *          `unique_ptr` 로 받음)과, 입력 · RHI 가 읽는 관찰 포인터. 소유권이 정적 칸을 거치면 누가 창을 파괴하는지가 경로마다 달랐고(그래서 `~IWindow` 가 전역을 끊었다),
 *          범위 없는 테스트가 그 칸을 서로 덮었다. 이제 **창은 이 서브시스템이 소유하고**(만든 쪽이 `adopt` 로 넘긴다), 읽는 쪽은 범위에서 찾는다.
 *          서브시스템은 창을 쓰는 서브시스템(RHI)보다 먼저 서고 나중에 내려가므로 창이 먼저 사라지는 순서가 없다. 이 서비스가 없는 호스트(서버 · 테스트 하네스)에서
 *          `findSubsystem` 은 nullptr 이고, 읽는 쪽은 "창 없음" 으로 다룬다(지금까지 `getActiveWindow()` 가 nullptr 이던 때와 같다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class IWindow;

    /**
     * @class MainWindowSubsystem
     * @brief 주 창 하나의 소유자입니다. 창을 만드는 일은 하지 않는다 — 만드는 쪽(RHI 가 창이 없을 때 만든 기본 창, App 이 직접 만든 창)이 `adopt` 로 넘긴다.
     */
    class SW_API MainWindowSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(창을 쓰는 서브시스템이 의존 항목으로 가리킨다). */
        static constexpr const utf8* kSubsystemName = "MainWindow";

        MainWindowSubsystem();
        ~MainWindowSubsystem() override;

        MainWindowSubsystem( const MainWindowSubsystem& )            = delete;
        MainWindowSubsystem& operator=( const MainWindowSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 창을 파괴합니다(네이티브 핸들을 놓고 객체를 지운다). 창을 쓰는 서브시스템은 이미 내려갔다. */
        void              shutdownSubsystem() override;
        const utf8*       getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask getSubsystemHost() const override;
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::EngineMisc; }

        /**
         * @brief 주 창을 넘겨받습니다. 이미 창이 있거나 @p window 가 비면 false 이고 아무것도 바꾸지 않습니다(두 번째 주 창을 조용히 바꿔 끼우지 않는다).
         * @details 성공하면 소유권은 이 서브시스템에 있다. 실패하면 @p window 는 부르는 쪽에 그대로 남아 부르는 쪽이 파괴한다.
         */
        [[nodiscard]] bool adopt( unique_ptr<IWindow>& window );

        /** @brief 주 창입니다. 아직 없으면 nullptr 입니다. 소유하지 않은 관찰 포인터로만 쓴다(이 서브시스템이 내려가면 무효). */
        IWindow* getWindow() const { return _window.get(); }

    private:
        unique_ptr<IWindow> _window;
    };
} // namespace sw
