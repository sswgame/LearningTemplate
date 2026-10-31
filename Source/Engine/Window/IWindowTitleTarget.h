/**
 * @file IWindowTitleTarget.h
 * @brief 창 제목을 거는 일의 이음매입니다 — 제목 주인(`AutomationRunTitleTracker`)이 창 대신 이것을 봅니다.
 * @details `IWindow::setTitle` 은 `MainWindowSubsystem` 만 부를 수 있고(friend), 그 서브시스템이 이 인터페이스를 구현한 멤버를 `getTitleTarget()` 으로만 내놓는다.
 *          그래서 창 제목을 거는 길은 하나다. 테스트는 가짜 구현을 넣어 "언제 몇 번 걸었는지" 를 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @class IWindowTitleTarget @brief 평소 제목을 읽고 최종 제목을 거는 곳입니다. */
    class IWindowTitleTarget
    {
    public:
        virtual ~IWindowTitleTarget() = default;

        /** @brief 지금 창이 가진 제목입니다(처음 한 번 평소 제목의 바탕으로 읽는다). */
        [[nodiscard]] virtual const utf8* getCurrentTitle() const = 0;
        /** @brief 호스트가 정한 평소 제목입니다. 정한 적이 없으면 빈 글입니다. */
        [[nodiscard]] virtual const string& getBaseTitle() const = 0;
        /** @brief 최종 제목을 창에 겁니다. 같은 제목이면 창이 건너뜁니다. */
        virtual void applyWindowTitle( const utf8* pTitle ) = 0;
    };
} // namespace sw
