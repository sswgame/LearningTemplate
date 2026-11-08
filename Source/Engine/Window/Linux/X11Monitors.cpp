#include "pch.h"

#include "Engine/Window/Linux/X11Monitors.h"

#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Window/WindowMonitor.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/X11Headers.h"

    #include <X11/extensions/Xrandr.h>

    #include "Core/Foundation/X11MacroUndef.h"

namespace sw
{
    namespace
    {
        /** @brief 올려 쓰는 libXrandr 함수들입니다. `decltype` 은 호출하지 않으므로 링크가 필요 없다. */
        struct X11RandrAPI
        {
            bool                                      _bLoaded{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            decltype( &XRRQueryExtension )            _pfnQueryExtension{ nullptr };
            decltype( &XRRQueryVersion )              _pfnQueryVersion{ nullptr };
            decltype( &XRRSelectInput )               _pfnSelectInput{ nullptr };
            decltype( &XRRUpdateConfiguration )       _pfnUpdateConfiguration{ nullptr };
            decltype( &XRRGetMonitors )               _pfnGetMonitors{ nullptr };
            decltype( &XRRFreeMonitors )              _pfnFreeMonitors{ nullptr };
            decltype( &XRRGetScreenResourcesCurrent ) _pfnGetScreenResourcesCurrent{ nullptr };
            decltype( &XRRFreeScreenResources )       _pfnFreeScreenResources{ nullptr };
            decltype( &XRRGetOutputInfo )             _pfnGetOutputInfo{ nullptr };
            decltype( &XRRFreeOutputInfo )            _pfnFreeOutputInfo{ nullptr };
            decltype( &XRRGetCrtcInfo )               _pfnGetCrtcInfo{ nullptr };
            decltype( &XRRFreeCrtcInfo )              _pfnFreeCrtcInfo{ nullptr };
        };

        struct X11MonitorsInternal
        {
            /** @brief XRRGetMonitors 가 생긴 RandR 판입니다(X.Org 1.18, 2015). 그 아래 서버는 RandR 이 없는 것과 같이 다룬다. */
            static constexpr int32 kRequiredMajor = 1;
            static constexpr int32 kRequiredMinor = 5;
            /** @brief `_NET_WORKAREA` 에서 읽는 최대 칸 수입니다(데스크톱 64 개). */
            static constexpr int64 kMaxWorkAreaLongs = 256;

            template <typename TFunction>
            static bool bind( void* pLibrary, const utf8* pName, TFunction& outFunction )
            {
                outFunction = reinterpret_cast<TFunction>( ModuleLibraryUtil::getDynamicSymbol( pLibrary, pName ) );
                return outFunction != nullptr;
            }

            /**
             * @brief 라이브러리를 프로세스에 한 번 올립니다. 내리지 않는다 - libXrandr 은 연결마다 닫기 훅(`XESetCloseDisplay`)을 걸어 먼저 내리면 `XCloseDisplay` 가 없는 코드로 뛴다.
             */
            static X11RandrAPI loadAPI()
            {
                X11RandrAPI api;
                void* const pLibrary = ModuleLibraryUtil::loadSystemLibrary( "libXrandr.so.2" );
                if ( pLibrary == nullptr )
                    return api;
                api._bLoaded = bind( pLibrary, "XRRQueryExtension", api._pfnQueryExtension ) && bind( pLibrary, "XRRQueryVersion", api._pfnQueryVersion ) &&
                               bind( pLibrary, "XRRSelectInput", api._pfnSelectInput ) && bind( pLibrary, "XRRUpdateConfiguration", api._pfnUpdateConfiguration ) &&
                               bind( pLibrary, "XRRGetMonitors", api._pfnGetMonitors ) && bind( pLibrary, "XRRFreeMonitors", api._pfnFreeMonitors ) &&
                               bind( pLibrary, "XRRGetScreenResourcesCurrent", api._pfnGetScreenResourcesCurrent ) && bind( pLibrary, "XRRFreeScreenResources", api._pfnFreeScreenResources ) &&
                               bind( pLibrary, "XRRGetOutputInfo", api._pfnGetOutputInfo ) && bind( pLibrary, "XRRFreeOutputInfo", api._pfnFreeOutputInfo ) &&
                               bind( pLibrary, "XRRGetCrtcInfo", api._pfnGetCrtcInfo ) && bind( pLibrary, "XRRFreeCrtcInfo", api._pfnFreeCrtcInfo );
                return api;
            }

            static const X11RandrAPI& getAPI()
            {
                static const X11RandrAPI s_api = loadAPI();
                return s_api;
            }

            /** @brief 뿌리 창의 CARDINAL 속성을 @p outListValue 에 담습니다. 없으면 false 입니다. */
            static bool readCardinals( Display* pDisplay, Window root, Atom property, int64 maxLongs, vector<int64>& outListValue )
            {
                outListValue.clear();
                Atom        type       = 0;
                int32       format     = 0;
                uint64      itemCount  = 0;
                uint64      bytesAfter = 0;
                uint8*      pData      = nullptr;
                const int32 result     = XGetWindowProperty( pDisplay, root, property, 0, maxLongs, 0, XA_CARDINAL, &type, &format, &itemCount, &bytesAfter, &pData );
                const bool  bRead      = result == 0 && type == XA_CARDINAL && format == 32 && pData != nullptr;
                if ( bRead )
                {
                    // 32 비트 형식의 속성은 Xlib 이 `long` 배열로 돌려준다.
                    const long* pValue = reinterpret_cast<const long*>( pData );
                    outListValue.assign( pValue, pValue + itemCount );
                }
                if ( pData != nullptr )
                    XFree( pData );
                return bRead;
            }

            /** @brief 지금 데스크톱의 작업 영역을 읽습니다. 창 관리자가 없거나(Xvfb) 적지 않았으면 false 입니다 - 뺄 패널을 알 길이 없어 작업 영역은 모니터 전체다. */
            static bool readDesktopWorkArea( Display* pDisplay, Window root, Atom workAreaAtom, WindowMonitorRect& outArea )
            {
                vector<int64> listValue;
                if ( readCardinals( pDisplay, root, workAreaAtom, kMaxWorkAreaLongs, listValue ) == false )
                    return false;
                // 현재 데스크톱을 적지 않는 창 관리자는 데스크톱이 하나다.
                vector<int64> listDesktop;
                const int64   desktopIndex = readCardinals( pDisplay, root, XInternAtom( pDisplay, "_NET_CURRENT_DESKTOP", 0 ), 1, listDesktop ) && listDesktop.empty() == false ? listDesktop[0] : 0;
                return WindowMonitor::tryParseNetWorkArea( listValue, desktopIndex, outArea );
            }

            /** @brief RandR 의 모드 표를 값으로 옮깁니다. 비월 주사 모드는 옵션 메뉴에 내지 않는다(Win32 도 DM_INTERLACED 를 고르지 않는 것이 보통이고 창 크기로는 같은 값이다). */
            static void readModeTable( const XRRScreenResources& resources, vector<WindowMonitorMode>& outListMode )
            {
                outListMode.clear();
                for ( int32 index = 0; index < resources.nmode; ++index )
                {
                    const XRRModeInfo& mode = resources.modes[index];
                    if ( ( mode.modeFlags & RR_Interlace ) != 0 )
                        continue;
                    outListMode.push_back( WindowMonitorMode{
                        static_cast<uint64>( mode.id ), int2{ static_cast<int32>( mode.width ), static_cast<int32>( mode.height ) }
                    } );
                }
            }

            /** @brief 모니터의 첫 출력이 내는 해상도를 적습니다. 지금 크기는 모드에 없어도(가상 출력 · `--scale`) 늘 선택지다. */
            static void readResolutions( Display* pDisplay, const X11RandrAPI& api, XRRScreenResources* pResources, const vector<WindowMonitorMode>& listMode, const XRRMonitorInfo& source, WindowMonitor& outMonitor )
            {
                outMonitor._listResolution.push_back( outMonitor._area._size );
                if ( pResources == nullptr || source.noutput <= 0 )
                    return;
                XRROutputInfo* const pOutput = api._pfnGetOutputInfo( pDisplay, pResources, source.outputs[0] );
                if ( pOutput == nullptr )
                    return;
                vector<uint64> listModeID;
                for ( int32 index = 0; index < pOutput->nmode; ++index )
                {
                    listModeID.push_back( static_cast<uint64>( pOutput->modes[index] ) );
                }
                bool bSwapAxes = false;
                if ( pOutput->crtc != 0 )
                {
                    XRRCrtcInfo* const pCrtc = api._pfnGetCrtcInfo( pDisplay, pResources, pOutput->crtc );
                    if ( pCrtc != nullptr )
                    {
                        bSwapAxes = ( pCrtc->rotation & ( RR_Rotate_90 | RR_Rotate_270 ) ) != 0;
                        api._pfnFreeCrtcInfo( pCrtc );
                    }
                }
                api._pfnFreeOutputInfo( pOutput );
                outMonitor.appendModeResolutions( listMode, listModeID, bSwapAxes );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    X11Monitors::X11Monitors()
        : _pDisplay{ nullptr }
        , _root{ 0 }
        , _workAreaAtom{ 0 }
        , _randrEventBase{ 0 }
        , _bRandrAvailable{ SW_FALSE }
    {
    }

    void X11Monitors::initialize( void* pDisplay )
    {
        using Internal           = X11MonitorsInternal;
        Display* const pXDisplay = static_cast<Display*>( pDisplay );
        _pDisplay                = pDisplay;
        _root                    = DefaultRootWindow( pXDisplay );
        _workAreaAtom            = XInternAtom( pXDisplay, "_NET_WORKAREA", 0 );
        _bRandrAvailable         = SW_FALSE;

        const X11RandrAPI& api = Internal::getAPI();
        if ( api._bLoaded == false )
        {
            SW_LOG_INFO_ID( "X11Monitors.NoRandrLibrary", "libXrandr.so.2 is not available - the X screen is the only monitor" );
            return;
        }
        int32 errorBase = 0;
        int32 major     = 0;
        int32 minor     = 0;
        if ( api._pfnQueryExtension( pXDisplay, &_randrEventBase, &errorBase ) == 0 || api._pfnQueryVersion( pXDisplay, &major, &minor ) == 0 )
        {
            SW_LOG_INFO_ID( "X11Monitors.NoRandrExtension", "This X server has no RandR extension - the X screen is the only monitor" );
            return;
        }
        const bool bNewEnough = major > Internal::kRequiredMajor || ( major == Internal::kRequiredMajor && minor >= Internal::kRequiredMinor );
        if ( bNewEnough == false )
        {
            SW_LOG_INFO_ID( "X11Monitors.OldRandr", "This X server has RandR %#.%# without monitors (1.5) - the X screen is the only monitor", major, minor );
            return;
        }
        _bRandrAvailable = SW_TRUE;
    }

    void X11Monitors::selectChangeEvents()
    {
        Display* const pXDisplay = static_cast<Display*>( _pDisplay );
        // 뿌리 창의 속성 알림은 이 연결의 것이다 - 다른 프로그램의 선택과 겹치지 않는다. 창 관리자가 패널을 옮기면 `_NET_WORKAREA` 가 바뀐다.
        XSelectInput( pXDisplay, static_cast<Window>( _root ), PropertyChangeMask );
        if ( _bRandrAvailable )
            X11MonitorsInternal::getAPI()._pfnSelectInput( pXDisplay, static_cast<Window>( _root ), RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask );
    }

    void X11Monitors::shutdown()
    {
        _pDisplay        = nullptr;
        _root            = 0;
        _bRandrAvailable = SW_FALSE;
    }

    bool X11Monitors::consumeEvent( void* pEvent, bool& outbChanged ) const
    {
        outbChanged            = false;
        XEvent&    event       = *static_cast<XEvent*>( pEvent );
        const bool bRandrEvent = _bRandrAvailable && ( event.type == _randrEventBase + RRScreenChangeNotify || event.type == _randrEventBase + RRNotify );
        if ( bRandrEvent )
        {
            X11MonitorsInternal::getAPI()._pfnUpdateConfiguration( &event ); // Xlib 이 든 screen 크기(`DisplayWidth`)도 맞춘다
            outbChanged = true;
            return true;
        }
        if ( event.type != PropertyNotify || event.xproperty.window != static_cast<Window>( _root ) )
            return false;
        outbChanged = event.xproperty.atom == static_cast<Atom>( _workAreaAtom );
        return true;
    }

    void X11Monitors::read( float32 contentScale, vector<WindowMonitor>& outListMonitor ) const
    {
        using Internal = X11MonitorsInternal;
        outListMonitor.clear();
        Display* const pXDisplay = static_cast<Display*>( _pDisplay );
        if ( pXDisplay == nullptr )
            return;
        const Window root = static_cast<Window>( _root );

        if ( _bRandrAvailable )
        {
            const X11RandrAPI&        api          = Internal::getAPI();
            int32                     monitorCount = 0;
            XRRMonitorInfo* const     pMonitors    = api._pfnGetMonitors( pXDisplay, root, 1, &monitorCount ); // 1 == True(활성 모니터만). X11MacroUndef.h 가 True 매크로를 지운다.
            XRRScreenResources* const pResources   = api._pfnGetScreenResourcesCurrent( pXDisplay, root );
            vector<WindowMonitorMode> listMode;
            if ( pResources != nullptr )
                Internal::readModeTable( *pResources, listMode );
            for ( int32 index = 0; index < monitorCount; ++index )
            {
                const XRRMonitorInfo& source = pMonitors[index];
                if ( source.width <= 0 || source.height <= 0 )
                    continue;
                WindowMonitor monitor;
                utf8* const   pName = source.name != 0 ? XGetAtomName( pXDisplay, source.name ) : nullptr;
                if ( pName != nullptr )
                {
                    monitor._name = pName;
                    XFree( pName );
                }
                monitor._area = WindowMonitorRect{
                    int2{    source.x,      source.y},
                    int2{source.width, source.height}
                };
                monitor._bPrimary = source.primary != 0;
                Internal::readResolutions( pXDisplay, api, pResources, listMode, source, monitor );
                outListMonitor.push_back( std::move( monitor ) );
            }
            if ( pResources != nullptr )
                api._pfnFreeScreenResources( pResources );
            if ( pMonitors != nullptr )
                api._pfnFreeMonitors( pMonitors );
        }

        // 머리말 참고: RandR 이 없거나 활성 모니터가 없다고 답하면 X screen 이 곧 화면이다. 크기 0 은 넣지 않는다 - 받는 쪽이 "화면 없음" 으로 읽는다.
        const int32 screen = DefaultScreen( pXDisplay );
        const int2  screenSize{ DisplayWidth( pXDisplay, screen ), DisplayHeight( pXDisplay, screen ) };
        if ( outListMonitor.empty() && screenSize._x > 0 && screenSize._y > 0 )
        {
            WindowMonitor monitor;
            monitor._name = "screen";
            monitor._area = WindowMonitorRect{
                int2{ 0, 0 },
                screenSize
            };
            monitor._listResolution.push_back( screenSize );
            outListMonitor.push_back( std::move( monitor ) );
        }

        WindowMonitorRect desktopWorkArea;
        const bool        bHasWorkArea = Internal::readDesktopWorkArea( pXDisplay, root, static_cast<Atom>( _workAreaAtom ), desktopWorkArea );
        for ( WindowMonitor& monitor : outListMonitor )
        {
            monitor._workArea     = monitor._area;
            monitor._contentScale = contentScale;
            if ( bHasWorkArea )
                monitor.clipWorkArea( desktopWorkArea );
            monitor.normalizeResolutions();
        }
        WindowMonitor::orderPrimaryFirst( outListMonitor );
    }
} // namespace sw
#endif
