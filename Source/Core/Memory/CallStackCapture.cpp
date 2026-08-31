#include "pch.h"

#include "Core/Memory/CallStackCapture.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
	static atomic<bool> s_bCallStackInitialized{ false };
	static mutex		s_symbolMutex{};

	void CallStackCapture::initialize()
	{
		bool bExpected = false;
		if ( s_bCallStackInitialized.compare_exchange_strong( bExpected, true ) == false )
			return;

		std::scoped_lock<mutex> lock{ s_symbolMutex };
#if defined( SW_PLATFORM_WINDOWS )
		SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
		HANDLE process = GetCurrentProcess();
		SymInitialize( process, nullptr, FALSE );
		SymRefreshModuleList( process );
#endif
	}

	void CallStackCapture::shutdown()
	{
		bool bExpected = true;
		if ( s_bCallStackInitialized.compare_exchange_strong( bExpected, false ) == false )
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

	string CallStackCapture::symbolize( const CallStack& stack )
	{
		if ( stack._frameCount == 0 )
			return "[Empty CallStack]";

		StringBuilder<2048>		sb;
		std::scoped_lock<mutex> lock{ s_symbolMutex };

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
		if ( symbols != nullptr )
		{
			for ( uint32 frameIndex = 0; frameIndex < stack._frameCount; ++frameIndex )
			{
				string symbolName = symbols[frameIndex];

				// dladdr 로 더 정확한 심볼 정보 시도
				Dl_info info;
				if ( dladdr( stack._arrFrame[frameIndex], &info ) && info.dli_sname )
				{
					int32 status	 = 0;
					utf8* pDemangled = abi::__cxa_demangle( info.dli_sname, nullptr, nullptr, &status );
					symbolName		 = ( status == 0 && pDemangled ) ? pDemangled : info.dli_sname;
					free( pDemangled );
				}

				sb.appendFormat( "  [%#] %#\n", frameIndex, symbolName.c_str() );
				if ( dladdr( stack._arrFrame[frameIndex], &info ) && info.dli_sname )
				{
					sb.appendFormat( " + 0x%#", Fmt( reinterpret_cast<uint64>( (utf8*)stack._arrFrame[frameIndex] - (utf8*)info.dli_saddr ), Format().hex() ) );
				}
			}
			free( symbols );
		}
#endif

		return string( sb.view() );
	}

} // namespace sw
