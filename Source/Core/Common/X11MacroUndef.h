/**
 * @file X11MacroUndef.h
 * @brief X11 이 흔한 이름으로 정의한 매크로(None · Bool · Status · True · False 등)를 지웁니다.
 * @details `Bool` · `Status` 는 X11 API 가 타입으로도 쓰므로 같은 이름의 typedef 로 다시 정의해 둡니다.
 *          include guard 가 없습니다 — Xlib · GLX 를 포함할 때마다 바로 뒤에 다시 include 합니다.
 */

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/Types.h"
    #if defined( None )
        #undef None
    #endif
    #if defined( Bool )
        #undef Bool
    #endif
typedef int32 Bool;
    #if defined( Status )
        #undef Status
    #endif
typedef int32 Status;
    #if defined( Success )
        #undef Success
    #endif
    #if defined( Always )
        #undef Always
    #endif
    #if defined( Above )
        #undef Above
    #endif
    #if defined( Below )
        #undef Below
    #endif
    #if defined( Complex )
        #undef Complex
    #endif
    #if defined( True )
        #undef True
    #endif
    #if defined( False )
        #undef False
    #endif
    #if defined( AnyKey )
        #undef AnyKey
    #endif
    #if defined( TileShape )
        #undef TileShape
    #endif
    #if defined( CursorShape )
        #undef CursorShape
    #endif
    #if defined( PixmapShape )
        #undef PixmapShape
    #endif
#endif
