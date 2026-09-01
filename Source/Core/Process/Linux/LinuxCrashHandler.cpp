#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"

#include <csignal>
#include <cstdio>

#if defined( SW_PLATFORM_LINUX )
	#include "Core/Common/PlatformOsHeaders.h"

SW_LOG_CALLER( "LinuxCrashHandler" );
namespace sw
{
	namespace
	{
		atomic<bool> s_bInstalled{ false };
		/** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
		atomic<bool> s_bReporting{ false };

		/**
		 * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
		 */
		void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
		{
			if ( s_bReporting.exchange( true ) )
				return;

			DeepCallStack stack{};
			CallStackCapture::captureFromContext( stack, pPlatformContext );

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
			reportCrash( signalName( signalNumber ), nullptr, nullptr );
			std::signal( signalNumber, SIG_DFL );
			std::raise( signalNumber );
		}
	} // namespace

	void CrashHandler::initialize()
	{
		if ( s_bInstalled.exchange( true ) )
			return;

		CallStackCapture::initialize();

		std::signal( SIGSEGV, &onFatalSignal );
		std::signal( SIGBUS, &onFatalSignal );
		std::signal( SIGILL, &onFatalSignal );
		std::signal( SIGFPE, &onFatalSignal );
		std::signal( SIGABRT, &onFatalSignal );

		SW_LOG_TRACE( "Crash handler installed." );
	}

	void CrashHandler::shutdown()
	{
		if ( s_bInstalled.exchange( false ) == false )
			return;

		std::signal( SIGSEGV, SIG_DFL );
		std::signal( SIGBUS, SIG_DFL );
		std::signal( SIGILL, SIG_DFL );
		std::signal( SIGFPE, SIG_DFL );
		std::signal( SIGABRT, SIG_DFL );

		CallStackCapture::shutdown();
	}
} // namespace sw

#endif
