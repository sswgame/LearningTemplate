/**
 * @file DevConsoleOverlay.h
 * @brief 에디터 없이 게임 창에서 쓰는 개발 콘솔 오버레이입니다(`~` 키로 연다). Shipping 에는 없습니다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Engine/Utility/Console/DevConsole.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    struct NativeWindowEvent;

    class IWindow;

    /** @brief 콘솔이 받는 키 하나입니다(플랫폼 창 이벤트를 풀어 만든다). */
    struct DevConsoleKey
    {
        enum class Kind : uint8
        {
            None = 0,
            Toggle,    ///< `~` — 열고 닫는다
            Character, ///< 글자 하나(`_codepoint`)
            Backspace,
            Enter,
            Tab,            ///< 자동완성
            HistoryBack,    ///< ↑
            HistoryForward, ///< ↓
            Close           ///< Esc
        };

        Kind   _kind{ Kind::None };
        uint32 _codepoint{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IDevConsoleWindow
     * @brief 오버레이를 화면에 그리는 플랫폼 창입니다(Win32: 게임 창이 소유한 GDI 팝업, X11: 게임 창의 자식 창). 판단은 `DevConsoleOverlay` 가 합니다.
     */
    class IDevConsoleWindow
    {
    public:
        virtual ~IDevConsoleWindow() = default;

        /** @brief 게임 창 이벤트를 콘솔 키로 풉니다. 콘솔 키가 아니면 false 입니다. */
        virtual bool decodeKey( const NativeWindowEvent& event, DevConsoleKey& outKey ) const = 0;
        /** @brief 창을 보이거나 숨깁니다. 보일 때도 게임 창의 키보드 포커스는 빼앗지 않습니다. */
        virtual void setVisible( bool bVisible ) = 0;
        /** @brief 그릴 줄(위에서 아래로)을 넘기고 게임 창 위로 자리를 다시 맞춥니다. @p listErrorFlag 가 0 이 아닌 줄은 오류 색입니다. */
        virtual void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) = 0;

        /** @brief 이 플랫폼의 창을 만듭니다. 만들지 못하면 nullptr(오버레이는 그리지 않고 키만 받습니다). */
        static unique_ptr<IDevConsoleWindow> createPlatform( IWindow& owner );
    };
} // namespace sw

namespace sw
{
    /**
     * @class DevConsoleOverlay
     * @brief 게임 창 위의 개발 콘솔입니다 — `~` 로 열고, 줄을 치고 Enter, Tab 자동완성, ↑↓ 기록, Esc · `~` 로 닫습니다.
     * @details 에디터가 없을 때 App 이 들고 창 이벤트를 먼저 보여 줍니다. 열려 있는 동안은 키보드 이벤트를 모두 가져가 게임 입력으로 새지 않게 합니다.
     *          판단(`handleKey` · `buildVisibleLines`)은 플랫폼 창 없이 시험합니다.
     */
    class SW_API DevConsoleOverlay
    {
    public:
        /** @brief 입력 줄 위에 보이는 출력 줄 수입니다. */
        static constexpr uint32 kVisibleOutputCount = 12;
        /** @brief 입력 줄의 최대 길이(바이트)입니다. */
        static constexpr uint32 kMaxInputLength = 512;

        DevConsoleOverlay();
        ~DevConsoleOverlay();

        /** @brief 게임 창 위에 그릴 플랫폼 창을 만듭니다. 만들지 못해도 키는 받습니다(false 를 돌려줍니다). */
        bool initialize( IWindow* pOwner );
        /** @brief 플랫폼 창을 없앱니다. */
        void shutdown();

        /** @brief 게임 창 이벤트를 봅니다. 콘솔이 가져갔으면(열려 있는 동안의 키보드 · 여는 키) true 입니다. */
        bool handleEvent( const NativeWindowEvent& event );
        /** @brief 키 하나를 처리합니다. 가져갔으면 true 입니다. */
        bool handleKey( const DevConsoleKey& key );
        /** @brief 바뀐 것이 있으면 플랫폼 창에 다시 그립니다. 프레임마다 부릅니다. */
        void update();

        bool          isOpen() const { return _bOpen == SW_TRUE; }
        void          setOpen( bool bOpen );
        const string& getInputLine() const { return _inputLine; }
        DevConsole&   getConsole() { return _console; }

        /** @brief 그릴 줄입니다 — 마지막 출력 `kVisibleOutputCount` 줄과 입력 줄(`] 입력_`). */
        void buildVisibleLines( vector<string>& outListLine, vector<uint8>& outListErrorFlag ) const;

    private:
        DevConsole                    _console;
        string                        _inputLine;
        unique_ptr<IDevConsoleWindow> _pWindow;
        uint8                         _bOpen    : 1;
        uint8                         _bDirty   : 1;
        [[maybe_unused]] uint8        _reserved : 6;
    };
} // namespace sw

#endif
