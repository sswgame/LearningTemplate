#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Container/StringUtil.h"
    #include "Core/Memory/Memory.h"

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "PlatformClipboard" );

    namespace
    {
        struct Win32ClipboardInternal
        {
            /** @brief 클립보드를 연 동안만 쥐는 범위입니다. 열지 못하면 `isOpen` 이 false 입니다. */
            class OpenScope
            {
            public:
                explicit OpenScope( HWND owner )
                    : _bOpen{ OpenClipboard( owner ) != FALSE }
                {
                }
                ~OpenScope()
                {
                    if ( _bOpen )
                        CloseClipboard();
                }
                OpenScope( const OpenScope& )            = delete;
                OpenScope& operator=( const OpenScope& ) = delete;

                bool isOpen() const { return _bOpen; }

            private:
                bool _bOpen;
            };

            /** @brief 클립보드 주인으로 둘 창입니다(활성 창). 없으면 nullptr 입니다. */
            static HWND findOwnerWindow()
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                return pWindow != nullptr ? static_cast<HWND>( pWindow->getNativeHandle() ) : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PlatformClipboard::isAvailable()
    {
        return true;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        if ( IsClipboardFormatAvailable( CF_UNICODETEXT ) == FALSE )
            return false;
        const Win32ClipboardInternal::OpenScope scope( Win32ClipboardInternal::findOwnerWindow() );
        if ( scope.isOpen() == false )
            return false;
        HANDLE hData = GetClipboardData( CF_UNICODETEXT );
        if ( hData == nullptr )
            return false;
        const utf16* pText = static_cast<const utf16*>( GlobalLock( hData ) );
        if ( pText == nullptr )
            return false;
        outText = StringUtil::utf16ToUtf8( pText );
        GlobalUnlock( hData );
        return true;
    }

    void PlatformClipboard::writeText( string_view text )
    {
        const HWND owner = Win32ClipboardInternal::findOwnerWindow();
        if ( owner == nullptr )
        {
            SW_LOG_WARNING( "Clipboard write needs an active window to own the clipboard - not written" );
            return;
        }
        const wstring                           wide = StringUtil::utf8ToUtf16( string( text ).c_str() );
        const Win32ClipboardInternal::OpenScope scope( owner );
        if ( scope.isOpen() == false || EmptyClipboard() == FALSE )
        {
            SW_LOG_WARNING( "Clipboard could not be opened - not written" );
            return;
        }
        const size_t byteSize = ( wide.size() + 1 ) * sizeof( utf16 );
        HGLOBAL      hMemory  = GlobalAlloc( GMEM_MOVEABLE, byteSize );
        if ( hMemory == nullptr )
        {
            SW_LOG_WARNING( "Clipboard memory (%# bytes) could not be allocated - not written", static_cast<uint64>( byteSize ) );
            return;
        }
        void* pDestination = GlobalLock( hMemory );
        if ( pDestination == nullptr )
        {
            GlobalFree( hMemory );
            SW_LOG_WARNING( "Clipboard memory could not be locked - not written" );
            return;
        }
        Memory::copy( pDestination, wide.c_str(), byteSize );
        GlobalUnlock( hMemory );
        // 성공하면 메모리는 시스템 것이다. 실패했을 때만 직접 놓는다.
        if ( SetClipboardData( CF_UNICODETEXT, hMemory ) == nullptr )
        {
            GlobalFree( hMemory );
            SW_LOG_WARNING( "Clipboard data could not be set - not written" );
        }
    }
} // namespace sw
#endif
