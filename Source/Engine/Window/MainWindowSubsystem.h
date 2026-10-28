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
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Window/FramePacer.h"
#include "Engine/Window/IWindowTitleTarget.h"

namespace sw
{
    class IWindow;

    /**
     * @class MainWindowSubsystem
     * @brief 주 창 하나의 소유자입니다. 창을 만드는 일은 하지 않는다 — 만드는 쪽(RHI 가 창이 없을 때 만든 기본 창, App 이 직접 만든 창)이 `adopt` 로 넘긴다.
     */
    class SW_API MainWindowSubsystem final : public ISubsystem, private IWindowTitleTarget
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

        /**
         * @brief 평소 창 제목(실행 정보를 붙이기 전의 제목)을 정합니다. 에디터가 프레임마다 씬 · 미저장 표시를 넣어 만든 제목을 여기에 건다.
         * @details 창에 직접 `setTitle` 하지 않는다 — 엔진이 프레임 끝에 이 제목 뒤에 시나리오 실행 정보를 붙여(`AutomationRunTitleTracker`) 창에 걸기 때문에,
         *          제목 주인이 하나다. 한 번도 정하지 않으면(빈 글) 엔진은 창이 처음 가진 제목을 평소 제목으로 쓴다.
         */
        void setBaseTitle( string_view title ) { _baseTitle = title; }
        /** @brief 정한 평소 창 제목입니다. 정한 적이 없으면 빈 글입니다. */
        const string& getBaseTitle() const override { return _baseTitle; }
        /** @brief 제목을 거는 이음매입니다(엔진의 `AutomationRunTitleTracker` 전용). 창이 없으면 nullptr 입니다. `IWindow::setTitle` 은 이 서브시스템만 부를 수 있다. */
        IWindowTitleTarget* getTitleTarget() { return _window != nullptr ? this : nullptr; }

        /** @brief 주 창을 도는 앱 루프의 프레임 페이싱입니다. App 루프가 프레임마다 갱신하고 감시 값 `Engine.FramePacingState` 가 읽는다. */
        FramePacer&       getFramePacer() { return _framePacer; }
        const FramePacer& getFramePacer() const { return _framePacer; }

    private:
        const utf8* getCurrentTitle() const override;
        void        applyWindowTitle( const utf8* pTitle ) override;

        unique_ptr<IWindow> _window;
        string              _baseTitle;
        FramePacer          _framePacer;
    };
} // namespace sw
