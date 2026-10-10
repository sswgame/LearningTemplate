/**
 * @file DevConsoleController.h
 * @brief 게임 창 개발 콘솔의 판단입니다 — 셸 InputMap 의 콘솔 액션과 글자 입력을 받아 한 줄을 편집 · 실행하고, 플랫폼 창에 그릴 줄을 넘깁니다.
 *        Shipping 에는 없습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Console/DevConsole.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    class IDevConsoleWindow;
    class InputManager;
    class InputMap;
    class IWindow;

    /**
     * @class DevConsoleController
     * @brief 에디터 없이 띄운 게임 창의 개발 콘솔입니다. 여는 키 · 편집 키는 셸 InputMap(`default.input.xml`)의 액션이고, 글자는 `InputManager` 의
     *        글자 입력(포커스 `DevConsole`)입니다.
     * @details 열려 있는 동안 `InputManager` 키보드 포커스를 쥡니다 — 게임 쪽 키 조회와 통합 InputMap 의 키보드 바인딩은 그동안 "안 눌림" 입니다.
     *          해석(명령 · `gv_` · 자동완성 · 기록)은 `DevConsole` 이 하고, 그리기는 `IDevConsoleWindow` 가 합니다. 시험은 플랫폼 창 없이
     *          `RawInputEvent` 를 넣어 돌립니다.
     */
    class SW_API DevConsoleController
    {
    public:
        /** @brief 입력 줄 위에 보이는 출력 줄 수입니다. */
        static constexpr uint32 kVisibleOutputCount = 12;
        /** @brief 입력 줄의 최대 길이(바이트)입니다. */
        static constexpr uint32 kMaxInputLength = 512;

        DevConsoleController();
        ~DevConsoleController();

        DevConsoleController( const DevConsoleController& )            = delete;
        DevConsoleController& operator=( const DevConsoleController& ) = delete;

        /**
         * @brief @p pInput 에 콘솔 몫 글자 콜백을 걸고, @p pOwner 가 있으면 그 위에 그릴 플랫폼 창을 만듭니다.
         * @return 그릴 창을 만들었으면 true 입니다. 만들지 못해도 입력은 받습니다.
         */
        bool initialize( InputManager* pInput, IWindow* pOwner );
        /** @brief 글자 콜백을 풀고 키보드 포커스를 게임에 돌려준 뒤 플랫폼 창을 없앱니다. `InputManager` 보다 먼저 부릅니다. */
        void shutdown();

        /**
         * @brief 셸 InputMap 의 콘솔 액션과 이번 프레임 글자를 처리하고, 열려 있으면 창에 그립니다. 셸 맵을 갱신한 **뒤** 프레임마다 부릅니다.
         * @details 열기 · 닫기가 발화한 프레임의 글자는 버립니다 — 여는 키가 같은 프레임에 내는 글자(`` ` `` · `~`)가 입력 줄에 들어가지 않습니다.
         *          콘솔 레이어(`InputMapDefaults::kDevConsoleLayerName`)를 열림에 맞춰 켜고 끕니다.
         */
        void update( InputMap& shellMap );

        bool isOpen() const { return _bOpen == SW_TRUE; }
        /** @brief 열고 닫습니다. 열면 키보드 포커스를 가져오고, 닫으면 게임에 돌려줍니다. */
        void          setOpen( bool bOpen );
        const string& getInputLine() const { return _inputLine; }
        DevConsole&   getConsole() { return _console; }

        /** @brief 그릴 줄입니다 — 마지막 출력 `kVisibleOutputCount` 줄과 입력 줄(`] 입력_`). */
        void collectVisibleLines( vector<string>& outListLine, vector<uint8>& outListErrorFlag ) const;

    private:
        /** @brief 포커스가 콘솔일 때 오는 글자입니다. 프레임 안에서 모았다가 `update` 가 붙이거나 버립니다. */
        void onTextInput( string_view text );
        /** @brief 글자를 입력 줄에 붙입니다. 제어 문자(Enter · Tab · Backspace 가 내는 것)는 버립니다. */
        void appendText( string_view text );
        /** @brief Tab — 입력 줄을 완성하고, 후보가 여럿이면 출력 줄에 늘어놓습니다. */
        void completeInputLine();
        /** @brief 셸 맵에 콘솔 레이어 · 액션이 다 있으면 true 입니다. 처음 한 번만 검사하고, 빠졌으면 오류를 남깁니다. */
        bool hasConsoleActions( const InputMap& shellMap );

    private:
        DevConsole                    _console;
        string                        _inputLine;
        string                        _pendingText; /**< 이번 프레임에 온 글자(`update` 가 비운다). */
        unique_ptr<IDevConsoleWindow> _pWindow;
        InputManager*                 _pInput;
        uint8                         _bOpen             : 1;
        uint8                         _bShellMapChecked  : 1; /**< 셸 맵을 한 번 검사했다. */
        uint8                         _bShellMapComplete : 1; /**< 검사한 셸 맵에 콘솔 레이어 · 액션이 다 있었다. */
        [[maybe_unused]] uint8        _reserved          : 5;
    };
} // namespace sw

#endif
