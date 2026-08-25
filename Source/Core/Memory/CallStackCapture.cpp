#include "pch.h"

#include "Core/Memory/CallStackCapture.h"

#include "Core/Concurrency/mutex.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformOsHeaders.h"
	#pragma warning( push )
	#pragma warning( disable : 4091 )
	#include <DbgHelp.h>
	#pragma warning( pop )
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
	#include <cxxabi.h>
	#include <dlfcn.h>
	#include <execinfo.h>
#endif

namespace sw
{
	static bool	 s_bCallStackInitialized{ false };
	static mutex s_symbolMutex{};

	void CallStackCapture::initialize()
	{
		if ( s_bCallStackInitialized )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
		HANDLE process = GetCurrentProcess();
		SymInitialize( process, nullptr, FALSE );
		SymRefreshModuleList( process );
#endif
		s_bCallStackInitialized = true;
	}

	void CallStackCapture::shutdown()
	{
		if ( s_bCallStackInitialized == false )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		HANDLE process = GetCurrentProcess();
		SymCleanup( process );
#endif
		s_bCallStackInitialized = false;
	}

	void CallStackCapture::capture( CallStack& outStack, uint32 skipFrames )
	{
		outStack.frameCount = 0;
		outStack.hash		= 0;

#if defined( SW_PLATFORM_WINDOWS )
		// 이 capture 함수 자신을 건너뛰기 위해 +1
		outStack.frameCount = CaptureStackBackTrace( skipFrames + 1, CallStack::kMaxFrames, outStack.frames, nullptr );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		void* tempFrames[CallStack::kMaxFrames + 16];
		int32 count		 = backtrace( tempFrames, CallStack::kMaxFrames + skipFrames + 1 );
		int32 validCount = count - ( skipFrames + 1 );
		if ( validCount > 0 )
		{
			outStack.frameCount = MathUtil::min<uint32>( static_cast<uint32>( validCount ), CallStack::kMaxFrames );
			for ( uint32 frameIndex = 0; frameIndex < outStack.frameCount; ++frameIndex )
			{
				outStack.frames[frameIndex] = tempFrames[frameIndex + skipFrames + 1];
			}
		}
#endif

		// 프레임 주소로 해시를 계산합니다.
		uint64 hash{ 0 };
		for ( uint32 frameIndex = 0; frameIndex < outStack.frameCount; ++frameIndex )
		{
			hash ^= reinterpret_cast<uint64>( outStack.frames[frameIndex] ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
		}
		outStack.hash = hash;
	}

	string CallStackCapture::symbolize( const CallStack& stack )
	{
		if ( stack.frameCount == 0 )
			return "[Empty CallStack]";

		StringBuilder<2048>		sb;
		std::scoped_lock<mutex> lock{ s_symbolMutex };

#if defined( SW_PLATFORM_WINDOWS )
		HANDLE						process = GetCurrentProcess();
		alignas( SYMBOL_INFO ) utf8 symbolBuffer[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME * sizeof( TCHAR )];
		SYMBOL_INFO*				pSymbol = reinterpret_cast<SYMBOL_INFO*>( symbolBuffer );
		pSymbol->SizeOfStruct				= sizeof( SYMBOL_INFO );
		pSymbol->MaxNameLen					= MAX_SYM_NAME;

		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof( IMAGEHLP_LINE64 );

		for ( uint32 frameIndex = 0; frameIndex < stack.frameCount; ++frameIndex )
		{
			DWORD64 address = reinterpret_cast<DWORD64>( stack.frames[frameIndex] );
			DWORD	displacementLine{ 0 };
			DWORD64 displacementSym{ 0 };

			sb.appendFormat( "[%#] ", frameIndex );

			if ( SymFromAddr( process, address, &displacementSym, pSymbol ) )
				sb.append( pSymbol->Name );
			else
				sb.appendFormat( "0x%#", Fmt( address, Format().hex() ) );

			if ( SymGetLineFromAddr64( process, address, &displacementLine, &line ) )
				sb.append( " (" ).append( line.FileName ).append( ":" ).append( static_cast<uint32>( line.LineNumber ) ).append( ")" );
			sb.append( "\n" );
		}
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		utf8** symbols = backtrace_symbols( stack.frames, stack.frameCount );
		if ( symbols )
		{
			for ( uint32 frameIndex = 0; frameIndex < stack.frameCount; ++frameIndex )
			{
				sb.appendFormat( "[%#] ", frameIndex );

				// 선택: abi::__cxa_demangle로 심볼을 복원합니다.
				Dl_info info;
				if ( dladdr( stack.frames[frameIndex], &info ) && info.dli_sname )
				{
					int32 status;
					utf8* demangled = abi::__cxa_demangle( info.dli_sname, nullptr, 0, &status );
					if ( status == 0 && demangled )
					{
						sb.append( demangled );
						// Allocated by abi::__cxa_demangle via libc malloc
						std::free( demangled );
					}
					else
					{
						sb.append( info.dli_sname );
					}
					sb.appendFormat( " + 0x%#", Fmt( reinterpret_cast<uint64>( (utf8*)stack.frames[frameIndex] - (utf8*)info.dli_saddr ), Format().hex() ) );
				}
				else
				{
					sb.append( symbols[frameIndex] );
				}
				sb.append( "\n" );
			}
			// Allocated by backtrace_symbols via libc malloc
			std::free( symbols );
		}
#endif

		return string( sb.view() );
	}

} // namespace sw
