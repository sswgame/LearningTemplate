/**
 * @file WindowMonitor.h
 * @brief 창 시스템이 알리는 모니터 하나와, 플랫폼이 읽은 값을 목록으로 맞추는 규칙입니다(`IWindow::tryGetMonitors`).
 * @details 값을 읽는 일은 플랫폼 창(Win32 `EnumDisplayMonitors`, X11 RandR, Wayland `wl_output`)이 하고, 맞추는 규칙은 여기 한 벌이다 - OS 헤더가 없어 단위 테스트가 가짜 값으로 돈다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 화면 좌표의 사각형입니다(`_NET_WORKAREA` 한 칸 · 모니터의 작업 영역). */
    struct WindowMonitorRect
    {
        int2 _position{};
        int2 _size{};
    };

    /** @brief 창 시스템의 디스플레이 모드 한 칸입니다. X11 은 모드가 번호로 오고 출력은 번호 목록만 들어 표를 찾아야 한다(`appendModeResolutions`). */
    struct WindowMonitorMode
    {
        uint64 _id{ 0 };
        int2   _size{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 모니터 하나입니다. 좌표 · 크기는 창 시스템의 화면 좌표(픽셀)입니다.
     * @details 언리얼 `FMonitorInfo` · Godot `DisplayServer::screen_*` · GLFW `glfwGetMonitors` 와 같은 자리다.
     */
    struct SW_API WindowMonitor
    {
        string            _name;                 ///< 창 시스템이 붙인 이름(`eDP-1` · `\\.\DISPLAY1`). 로그와 모니터 고르기에 쓴다
        vector<int2>      _listResolution;       ///< 이 모니터가 내는 해상도(작은 것부터, 중복 없음). 설정 화면의 선택지다
        WindowMonitorRect _area;                 ///< 모니터 전체
        WindowMonitorRect _workArea;             ///< 작업 표시줄 · 패널을 뺀 영역
        float32           _contentScale{ 1.0f }; ///< 이 모니터의 권장 배율(96 DPI = 1). 물리 크기로 재지 않는다(`IWindow::getContentScale` 과 같은 출처)
        bool              _bPrimary{ false };    ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief 해상도 목록에 넣는 가장 작은 변(픽셀)입니다. 옛 모드(640x480 등)는 옵션 메뉴에 필요 없다. */
        static constexpr int32 kMinResolutionSide = 640;

        /**
         * @brief EWMH `_NET_WORKAREA`(데스크톱마다 x · y · 너비 · 높이)에서 데스크톱 @p desktopIndex 의 칸을 읽습니다.
         * @return 값이 그 데스크톱까지 없거나 크기가 0 이면 false 입니다(창 관리자가 없거나 적지 않았다).
         */
        [[nodiscard]] static bool tryParseNetWorkArea( const vector<int64>& listValue, int64 desktopIndex, WindowMonitorRect& outArea );
        /**
         * @brief 작업 영역을 데스크톱 전체의 작업 영역 @p desktopWorkArea 와 모니터의 겹침으로 정합니다.
         * @details `_NET_WORKAREA` 는 모니터가 여럿이어도 데스크톱에 하나라 모니터 몫은 겹침뿐이다(GLFW 와 같다). 겹치지 않으면 모니터 전체다 - 그 모니터에 창 관리자가 뺀 것이 없다.
         */
        void clipWorkArea( const WindowMonitorRect& desktopWorkArea );
        /** @brief 출력이 내는 모드 번호 @p listModeId 를 표 @p listMode 에서 찾아 해상도 목록에 더합니다. @p bSwapAxes 는 90 도 돌린 출력이다(모드 크기는 돌리기 전 값이다). 표에 없는 번호는 건너뛴다. */
        void appendModeResolutions( const vector<WindowMonitorMode>& listMode, const vector<uint64>& listModeId, bool bSwapAxes );
        /** @brief 해상도 목록에서 `kMinResolutionSide` 보다 작은 것을 빼고 작은 것부터 중복 없이 정렬합니다. */
        void normalizeResolutions();

        /** @brief 주 모니터를 앞으로 옮깁니다(ImGui 는 0 번을 주 모니터로 읽는다). 표시가 없으면 첫 모니터가 주 모니터다 - RandR 은 주 출력을 정하지 않은 설정이 흔하다. */
        static void orderPrimaryFirst( vector<WindowMonitor>& inoutListMonitor );
        /** @brief 주 모니터입니다(`orderPrimaryFirst` 를 거친 목록의 0 번). 목록이 비었으면 nullptr 입니다. */
        static const WindowMonitor* findPrimary( const vector<WindowMonitor>& listMonitor ) { return listMonitor.empty() ? nullptr : &listMonitor[0]; }
    };
} // namespace sw
