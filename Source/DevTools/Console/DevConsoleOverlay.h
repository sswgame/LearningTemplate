/**
 * @file DevConsoleOverlay.h
 * @brief 개발 콘솔 줄을 런타임 UI 오버레이로 그립니다(개발 도구 `DevTools`). 플랫폼 창을 따로 띄우지 않는다 — 언리얼 `UConsole` 이 뷰포트 캔버스에 그리는 것과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/UI/Screen/UIScreenTypes.h"

namespace sw
{
    class UISystem;

    /**
     * @class DevConsoleOverlay
     * @brief 게임 창 위쪽에 출력 줄과 입력 줄을 그리는 오버레이 화면 하나입니다. 그리기만 한다 — 키보드는 `InputManager` 의 콘솔 포커스로 받는다.
     * @details 화면은 오버레이 레이어라서 포커스 · 포인터 · 게임 입력을 받지 않는다. 줄 위젯은 처음 보일 때 정해진 수만큼 만들고 글만 바꾼다.
     */
    class DevConsoleOverlay
    {
    public:
        explicit DevConsoleOverlay( UISystem& ui );
        ~DevConsoleOverlay();
        DevConsoleOverlay( const DevConsoleOverlay& )            = delete;
        DevConsoleOverlay& operator=( const DevConsoleOverlay& ) = delete;

        /** @brief 화면을 열거나 닫습니다. */
        void setVisible( bool bVisible );
        /** @brief 그릴 줄(위에서 아래로)을 넘깁니다. @p listErrorFlag 가 0 이 아닌 줄은 오류 색, 마지막 줄(입력 줄)은 입력 색으로 그립니다. */
        void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag );

    private:
        UISystem*      _pUI;
        UIScreenHandle _screen; ///< 오버레이 화면입니다(닫혀 있으면 무효).
    };
} // namespace sw
