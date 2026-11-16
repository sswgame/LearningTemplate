/**
 * @file X11Monitors.h
 * @brief X 서버의 모니터 목록을 읽습니다(RandR · `_NET_WORKAREA`). 헤더에 X11 타입을 두지 않아 어느 플랫폼의 `IWindow.cpp` 에서도 읽힌다.
 * @details **libXrandr 은 링크하지 않고 올려 쓴다**(`libasound` · GLFW · SDL 과 같다) - X 서버 없이 도는 기계에도 깔린 게임이 동적 링커 단계에서 서지 않게 한다.
 *          헤더만 빌드에 필요하다(libxrandr-dev, `cmake/Toolchain/Platform/Linux.cmake` 가 확인한다).
 *
 *          **RandR 이 없어도 오류가 아니다.** 라이브러리가 없거나, 서버에 확장 · 1.5 판이 없거나(Xnest · 옛 VNC), 서버가 활성 모니터가 없다고 답하는 환경은 정상으로 있다.
 *          그때 코어 프로토콜의 X screen 은 추측값이 아니라 그 서버가 가진 화면 자체이므로 모니터 하나로 알린다(GLFW 와 같다). 해상도 선택지는 그 크기 하나다.
 *          이것과 달리 screen 크기까지 0 이면 쓸 수 있는 화면이 없는 것이라 받는 쪽(에디터 초기화)이 오류로 멈춘다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct WindowMonitor;

    /** @class X11Monitors @brief 연결 하나의 모니터 질의입니다. 값을 들고 있지 않는다 - 부르는 쪽이 세대(`IWindow::getMonitorGeneration`)로 다시 읽을 때를 안다. */
    class X11Monitors
    {
    public:
        X11Monitors();

        /** @brief @p pDisplay(`Display*`)의 기본 screen 에 RandR 이 있는지 봅니다. 읽기만 하는 곳(스플래시)은 이것으로 충분하다. */
        void initialize( void* pDisplay );
        /** @brief 모니터 변화 알림을 받기 시작합니다(RandR 변화, 뿌리 창의 `_NET_WORKAREA` 속성). 이벤트 루프를 도는 창만 부른다. */
        void selectChangeEvents();
        /** @brief 연결을 잊습니다. 연결을 닫기 전에 부른다. */
        void shutdown();

        /**
         * @brief @p pEvent(`XEvent*`)가 뿌리 창으로 온 모니터 소식이면 처리하고 true 입니다. 창 처리기에 넘길 이벤트가 아니다.
         * @param outbChanged 모니터 값이 바뀌었으면 true 입니다(다른 속성의 알림이면 false).
         */
        [[nodiscard]] bool consumeEvent( void* pEvent, bool& outbChanged ) const;

        /**
         * @brief 모니터를 @p outListMonitor 에 담습니다(주 모니터가 0 번, 서버 왕복이 있다). 머리말대로 RandR 이 없으면 X screen 하나입니다.
         * @param contentScale 모니터마다 적는 배율입니다. X 서버에는 데스크톱에 하나(`Xft.dpi`)뿐이다(`X11Window::getContentScale`).
         */
        void read( float32 contentScale, vector<WindowMonitor>& outListMonitor ) const;

    private:
        void*  _pDisplay;     ///< `Display*`
        uint64 _root;         ///< 기본 screen 의 뿌리 창
        uint64 _workAreaAtom; ///< `_NET_WORKAREA`
        int32  _randrEventBase;
        uint8  _bRandrAvailable : 1;
    };
} // namespace sw
