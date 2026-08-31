#include "pch.h"

#include "Core/Concurrency/CrashHandler.h"

#include "Core/Memory/CallStackCapture.h"
#include "Core/String/StringBuilder.h"

#include <cstdio>

#if defined( SW_PLATFORM_WINDOWS )
	#include <windows.h>
#else
	#include <csignal>
#endif

SW_LOG_CALLER( "CrashHandler" );
namespace sw
{
	namespace
	{
		atomic<bool> s_bInstalled{ false };
		/** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
		atomic<bool> s_bReporting{ false };

		/** @brief 폴트 종류와 현재 스레드 콜 스택을 로그로 남깁니다. */
		void reportCrash( const utf8* pReason, const void* pFaultAddress )
		{
			if ( s_bReporting.exchange( true ) )
				return;

			CallStack stack{};
			CallStackCapture::capture( stack, 1 );

			StringBuilder<constant::kMaxBuffer8192> builder;
			builder.append( "\n==================== CRASH ====================\n" );
			builder.append( pReason != nullptr ? pReason : "unknown fault" );
			if ( pFaultAddress != nullptr )
			{
				builder.append( "\n  at address: " );
				builder.append( reinterpret_cast<uint64>( pFaultAddress ) );
			}
			builder.append( "\n----------------- call stack ------------------\n" );
			builder.append( CallStackCapture::symbolize( stack ).c_str() );
			builder.append( "===============================================\n" );

			// 로거가 비동기일 수 있으므로 stderr 로도 직접 흘려 크래시 직전 기록을 보장한다.
			std::fputs( builder.c_str(), stderr );
			std::fflush( stderr );
			SW_LOG_ERROR( "%#", builder.c_str() );

			s_bReporting.store( false );
		}

#if defined( SW_PLATFORM_WINDOWS )
		LPTOP_LEVEL_EXCEPTION_FILTER s_pPreviousFilter{ nullptr };

		/** @brief 예외 코드를 사람이 읽는 이름으로 바꿉니다. */
		const utf8* exceptionCodeName( DWORD code )
		{
			switch ( code )
			{
				case EXCEPTION_ACCESS_VIOLATION:
					return "EXCEPTION_ACCESS_VIOLATION (segfault)";
				case EXCEPTION_STACK_OVERFLOW:
					return "EXCEPTION_STACK_OVERFLOW";
				case EXCEPTION_ILLEGAL_INSTRUCTION:
					return "EXCEPTION_ILLEGAL_INSTRUCTION";
				case EXCEPTION_INT_DIVIDE_BY_ZERO:
					return "EXCEPTION_INT_DIVIDE_BY_ZERO";
				case EXCEPTION_FLT_DIVIDE_BY_ZERO:
					return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
				case EXCEPTION_DATATYPE_MISALIGNMENT:
					return "EXCEPTION_DATATYPE_MISALIGNMENT";
				case EXCEPTION_IN_PAGE_ERROR:
					return "EXCEPTION_IN_PAGE_ERROR";
				case EXCEPTION_PRIV_INSTRUCTION:
					return "EXCEPTION_PRIV_INSTRUCTION";
				default:
					return "unhandled SEH exception";
			}
		}

		LONG WINAPI onUnhandledException( EXCEPTION_POINTERS* pInfo )
		{
			const void* pFaultAddress = nullptr;
			const utf8* pReason		  = "unhandled SEH exception";
			if ( pInfo != nullptr && pInfo->ExceptionRecord != nullptr )
			{
				pReason		  = exceptionCodeName( pInfo->ExceptionRecord->ExceptionCode );
				pFaultAddress = pInfo->ExceptionRecord->ExceptionAddress;
			}
			reportCrash( pReason, pFaultAddress );

			if ( s_pPreviousFilter != nullptr )
				return s_pPreviousFilter( pInfo );
			return EXCEPTION_EXECUTE_HANDLER;
		}
#else
		/** @brief 시그널 번호를 이름으로 바꿉니다. */
		const utf8* signalName( int32 signalNumber )
		{
			switch ( signalNumber )
			{
				case SIGSEGV:
					return "SIGSEGV (segfault)";
				case SIGBUS:
					return "SIGBUS";
				case SIGILL:
					return "SIGILL";
				case SIGFPE:
					return "SIGFPE";
				case SIGABRT:
					return "SIGABRT";
				default:
					return "fatal signal";
			}
		}

		void onFatalSignal( int32 signalNumber )
		{
			reportCrash( signalName( signalNumber ), nullptr );
			std::signal( signalNumber, SIG_DFL );
			std::raise( signalNumber );
		}
#endif
	} // namespace

	void CrashHandler::initialize()
	{
		if ( s_bInstalled.exchange( true ) )
			return;

		CallStackCapture::initialize();

#if defined( SW_PLATFORM_WINDOWS )
		s_pPreviousFilter = SetUnhandledExceptionFilter( &onUnhandledException );
#else
		std::signal( SIGSEGV, &onFatalSignal );
		std::signal( SIGBUS, &onFatalSignal );
		std::signal( SIGILL, &onFatalSignal );
		std::signal( SIGFPE, &onFatalSignal );
		std::signal( SIGABRT, &onFatalSignal );
#endif
		SW_LOG_TRACE( "Crash handler installed." );
	}

	void CrashHandler::shutdown()
	{
		if ( s_bInstalled.exchange( false ) == false )
			return;

#if defined( SW_PLATFORM_WINDOWS )
		SetUnhandledExceptionFilter( s_pPreviousFilter );
		s_pPreviousFilter = nullptr;
#else
		std::signal( SIGSEGV, SIG_DFL );
		std::signal( SIGBUS, SIG_DFL );
		std::signal( SIGILL, SIG_DFL );
		std::signal( SIGFPE, SIG_DFL );
		std::signal( SIGABRT, SIG_DFL );
#endif
	}
} // namespace sw
