/**
 * @file XTestInputInjector.h
 * @brief X 서버에 마우스 · 키 입력을 주입하는 XTest 주입기입니다(X11 창 입력 시험용).
 * @details 서버가 진짜 장치처럼 받아들이는 입력이라 창 쪽 코드(좌표 · 키 코드 변환 · `XkbSetDetectableAutoRepeat` 자동 반복)가 실제 경로로 돈다.
 *          `XSendEvent` 로는 이 경로를 만들 수 없다(자동 반복은 서버가 키를 누르고 있는 동안 만든다).
 *          상용 엔진과 같은 선: 언리얼 Automation Driver 와 유니티 Input Test Fixture 도 앱 안 이벤트 합성이 아니라 장치 층에서 입력을 넣는다.
 *          `libxtst`(CMake `SW_ENABLE_XTEST`)는 선택 의존이다 — 꺼져 있으면 `isCompiledIn()` 이 false 이고 나머지는 실패를 돌려준다(폴백 없음).
 * @note 리눅스 클라이언트 전용입니다. 주입기는 자기 X 연결을 따로 연다(창과 무관). 개발 명령은 `input.xtest`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @class XTestInputInjector
     * @brief XTest 확장으로 X 서버에 입력을 주입합니다. 좌표는 루트 창(화면) 기준입니다.
     */
    class XTestInputInjector
    {
    public:
        XTestInputInjector();
        ~XTestInputInjector();
        XTestInputInjector( const XTestInputInjector& )            = delete;
        XTestInputInjector& operator=( const XTestInputInjector& ) = delete;

        /** @brief 이 빌드에 XTest 가 들어 있는지(`SW_WITH_XTEST`)입니다. */
        [[nodiscard]] static bool isCompiledIn();

        /** @brief X 서버에 연결하고 XTest 확장이 있는지 확인합니다. 실패 이유는 @p outReason 에 적습니다. */
        [[nodiscard]] bool open( string& outReason );

        /** @brief 포인터를 화면 좌표로 옮깁니다. */
        [[nodiscard]] bool moveTo( int32 screenX, int32 screenY );
        /** @brief 마우스 버튼(1 왼쪽 · 2 가운데 · 3 오른쪽 · 4/5 휠 위/아래)을 누르거나 뗍니다. */
        [[nodiscard]] bool setButton( uint32 button, bool bPressed );
        /** @brief 키(X keysym 이름, 예: `a` · `Return` · `Shift_L`)를 누르거나 뗍니다. 이름이나 키 코드를 못 찾으면 false 입니다. */
        [[nodiscard]] bool setKey( const string& keysymName, bool bPressed );

    private:
        void* _pDisplay; ///< `Display*` — 헤더에 X11 을 끌고 오지 않으려고 불투명하게 둔다.
    };
} // namespace sw
