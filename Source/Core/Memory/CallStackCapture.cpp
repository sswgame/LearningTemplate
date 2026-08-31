#include "pch.h"

#include "Core/Memory/CallStackCapture.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
	// DeadlockDetector / MemoryProfiler / CrashHandler 가 각자 initialize·shutdown 한다.
	// bool 래치로 두면 먼저 shutdown 한 쪽이 SymCleanup 을 불러 나머지의 심볼화가 죽는다
	// (특히 종료 중 크래시에서 스택을 못 남긴다). 참조 카운트로 마지막 소유자만 정리한다.
	static atomic<int32> s_initRefCount{ 0 };
	static mutex		 s_symbolMutex{};

	void CallStackCapture::initialize()
	{
		if ( s_initRefCount.fetch_add( 1, std::memory_order_acq_rel ) != 0 )
			return;

		std::scoped_lock<mutex> lock{ s_symbolMutex };
#if defined( SW_PLATFORM_WINDOWS )
		SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES );
		HANDLE process = GetCurrentProcess();
		SymInitialize( process, nullptr, FALSE );
		SymRefreshModuleList( process );
#endif
	}

	void CallStackCapture::shutdown()
	{
		const int32 previous = s_initRefCount.fetch_sub( 1, std::memory_order_acq_rel );
		if ( previous <= 0 )
		{
			// 짝이 안 맞는 shutdown. 카운트를 음수로 두지 않는다.
			s_initRefCount.store( 0, std::memory_order_release );
			return;
		}
		if ( previous != 1 )
			return;

		std::scoped_lock<mutex> lock{ s_symbolMutex };
#if defined( SW_PLATFORM_WINDOWS )
		HANDLE process = GetCurrentProcess();
		SymCleanup( process );
#endif
	}

	void CallStackCapture::capture( CallStack& outStack, uint32 skipFrames )
	{
		outStack._frameCount = 0;
		outStack._hash		 = 0;

#if defined( SW_PLATFORM_WINDOWS )
		// 이 capture 함수 자신을 건너뛰기 위해 +1
		outStack._frameCount = CaptureStackBackTrace( skipFrames + 1, CallStack::kMaxFrames, outStack._arrFrame, nullptr );
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		void* tempFrames[CallStack::kMaxFrames + 16];
		int32 count		 = backtrace( tempFrames, CallStack::kMaxFrames + skipFrames + 1 );
		int32 validCount = count - ( skipFrames + 1 );
		if ( validCount > 0 )
		{
			outStack._frameCount = MathUtil::min<uint32>( static_cast<uint32>( validCount ), CallStack::kMaxFrames );
			for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
			{
				outStack._arrFrame[frameIndex] = tempFrames[frameIndex + skipFrames + 1];
			}
		}
#endif

		// 프레임 주소로 해시를 계산합니다.
		uint64 hash{ 0 };
		for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
		{
			hash ^= reinterpret_cast<uint64>( outStack._arrFrame[frameIndex] ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
		}
		outStack._hash = hash;
	}

	void CallStackCapture::captureFromContext( CallStack& outStack, void* pPlatformContext )
	{
		outStack._frameCount = 0;
		outStack._hash		 = 0;

#if defined( SW_PLATFORM_WINDOWS )
		if ( pPlatformContext == nullptr )
		{
			capture( outStack, 1 );
			return;
		}

		// StackWalk64 는 넘겨준 CONTEXT 를 진행하며 수정하므로 복사본으로 걷는다.
		CONTEXT walkContext = *static_cast<const CONTEXT*>( pPlatformContext );

		STACKFRAME64 frame{};
		DWORD		 machineType;
	#if defined( _M_X64 )
		machineType			   = IMAGE_FILE_MACHINE_AMD64;
		frame.AddrPC.Offset	   = walkContext.Rip;
		frame.AddrFrame.Offset = walkContext.Rbp;
		frame.AddrStack.Offset = walkContext.Rsp;
	#elif defined( _M_ARM64 )
		machineType			   = IMAGE_FILE_MACHINE_ARM64;
		frame.AddrPC.Offset	   = walkContext.Pc;
		frame.AddrFrame.Offset = walkContext.Fp;
		frame.AddrStack.Offset = walkContext.Sp;
	#else
		capture( outStack, 1 );
		return;
	#endif
		frame.AddrPC.Mode	 = AddrModeFlat;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Mode = AddrModeFlat;

		HANDLE					process = GetCurrentProcess();
		HANDLE					thread	= GetCurrentThread();
		std::scoped_lock<mutex> lock{ s_symbolMutex }; // StackWalk64 도 dbghelp 전역 상태를 쓴다.
		while ( outStack._frameCount < CallStack::kMaxFrames )
		{
			if ( StackWalk64( machineType, process, thread, &frame, &walkContext,
							  nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr ) == FALSE )
				break;
			if ( frame.AddrPC.Offset == 0 )
				break;
			outStack._arrFrame[outStack._frameCount++] = reinterpret_cast<void*>( frame.AddrPC.Offset );
		}
#else
		(void)pPlatformContext;
		capture( outStack, 1 );
		return;
#endif

		uint64 hash{ 0 };
		for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
		{
			hash ^= reinterpret_cast<uint64>( outStack._arrFrame[frameIndex] ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
		}
		outStack._hash = hash;
	}

	string CallStackCapture::symbolize( const CallStack& stack )
	{
		if ( stack._frameCount == 0 )
			return "[Empty CallStack]";

		// 32프레임 × (심볼 + 전체 파일 경로 + 라인)은 2KB 를 쉽게 넘긴다.
		StringBuilder<constant::kMaxBuffer8192> sb;

		// 크래시 경로에서도 불리므로 절대 막히면 안 된다. 다른 스레드가 심볼화 중이면
		// 교착 대신 주소만 출력한다(맵 파일로 후처리 가능).
		std::unique_lock<mutex> lock{ s_symbolMutex, std::try_to_lock };
		if ( lock.owns_lock() == false )
		{
			for ( uint32 frameIndex = 0; frameIndex < stack._frameCount; ++frameIndex )
			{
				sb.appendFormat( "  [%#] 0x%# (symbols busy)\n", frameIndex,
								 Fmt( reinterpret_cast<uint64>( stack._arrFrame[frameIndex] ), Format().hex() ) );
			}
			return string( sb.view() );
		}

#if defined( SW_PLATFORM_WINDOWS )
		HANDLE						process = GetCurrentProcess();
		alignas( SYMBOL_INFO ) utf8 symbolBuffer[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME * sizeof( TCHAR )];
		SYMBOL_INFO*				pSymbol = reinterpret_cast<SYMBOL_INFO*>( symbolBuffer );
		pSymbol->SizeOfStruct				= sizeof( SYMBOL_INFO );
		pSymbol->MaxNameLen					= MAX_SYM_NAME;

		for ( uint32 frameIndex = 0; frameIndex < stack._frameCount; ++frameIndex )
		{
			DWORD64 address = reinterpret_cast<DWORD64>( stack._arrFrame[frameIndex] );
			if ( address == 0 )
				continue;

			DWORD64 displacement = 0;
			if ( SymFromAddr( process, address, &displacement, pSymbol ) )
			{
				DWORD			displacementLine = 0;
				IMAGEHLP_LINE64 lineInfo		 = {};
				lineInfo.SizeOfStruct			 = sizeof( IMAGEHLP_LINE64 );

				if ( SymGetLineFromAddr64( process, address, &displacementLine, &lineInfo ) )
				{
					sb.appendFormat( "  [%#] %# (%#:%#)\n", frameIndex, pSymbol->Name, lineInfo.FileName, lineInfo.LineNumber );
				}
				else
				{
					sb.appendFormat( "  [%#] %#\n", frameIndex, pSymbol->Name );
				}
			}
			else
			{
				sb.appendFormat( "  [%#] 0x%#\n", frameIndex, Fmt( address, Format().hex() ) );
			}
		}
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
		utf8** symbols = backtrace_symbols( stack._arrFrame, stack._frameCount );
		for ( uint32 frameIndex = 0; frameIndex < stack._frameCount; ++frameIndex )
		{
			string symbolName = ( symbols != nullptr ) ? symbols[frameIndex] : "??";

			// dladdr 로 더 정확한 심볼 정보 시도 (한 번만 호출하고 결과를 재사용한다)
			Dl_info	   info{};
			const bool bResolved = ( dladdr( stack._arrFrame[frameIndex], &info ) != 0 && info.dli_sname != nullptr );
			if ( bResolved )
			{
				int32 status	 = 0;
				utf8* pDemangled = abi::__cxa_demangle( info.dli_sname, nullptr, nullptr, &status );
				symbolName		 = ( status == 0 && pDemangled != nullptr ) ? pDemangled : info.dli_sname;
				free( pDemangled );
			}

			sb.appendFormat( "  [%#] %#", frameIndex, symbolName.c_str() );
			if ( bResolved && info.dli_saddr != nullptr )
			{
				// 포인터 차이는 정수(ptrdiff_t)이므로 reinterpret_cast 가 아니라 정수 변환을 쓴다.
				const ptrdiff_t frameOffset =
					static_cast<const utf8*>( stack._arrFrame[frameIndex] ) - static_cast<const utf8*>( info.dli_saddr );
				sb.appendFormat( " + 0x%#", Fmt( static_cast<uint64>( frameOffset ), Format().hex() ) );
			}
			sb.append( "\n" );
		}
		free( symbols );
#endif

		return string( sb.view() );
	}

} // namespace sw
