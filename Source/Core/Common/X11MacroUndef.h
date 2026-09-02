/**
 * @file X11MacroUndef.h
 * @brief X11 매크로를 해제하고 C++ 호환 타입 정의(Bool, Status)를 유지합니다. include guard가 없습니다 — Xlib/GLX 포함 직후마다 다시 include 합니다.
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
