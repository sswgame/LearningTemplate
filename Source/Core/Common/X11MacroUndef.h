/**
 * @file X11MacroUndef.h
 * @brief X11 매크로를 해제합니다. include guard가 없습니다 — Xlib/GLX 포함 직후마다 다시 include 합니다.
 */

#if defined( SW_PLATFORM_LINUX )
	#if defined( None )
		#undef None
	#endif
	#if defined( Bool )
		#undef Bool
	#endif
	#if defined( Status )
		#undef Status
	#endif
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
#endif
