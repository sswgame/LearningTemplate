#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"

#include <cstdio>

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformOsHeaders.h"

SW_LOG_CALLER( "WindowsCrashHandler" );
namespace sw
{
	namespace
	{
		atomic<bool> s_bInstalled{ false };
		/** @brief 핸들러 안에서 또 크래시가 나도 무한 재진입하지 않게 막습니다. */
		atomic<bool> s_bReporting{ false };

		/**
		 * @brief 폴트 종류와 콜 스택을 로그로 남깁니다.
		 * @param pPlatformContext 있으면 그 지점부터 스택을 걷습니다(Windows 는 CONTEXT*).
		 *                         nullptr 이면 현재 스레드 스택을 캡처합니다.
		 */
		void reportCrash( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext )
		{
			if ( s_bReporting.exchange( true ) )
				return;

			// 예외 컨텍스트에서 걸어야 KiUserExceptionDispatcher 등 디스패치 프레임이 앞을
			// 차지하지 않고 실제 폴트 지점이 [0] 에 온다.
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
			reportCrash( pReason, pFaultAddress, ( pInfo != nullptr ) ? pInfo->ContextRecord : nullptr );

			if ( s_pPreviousFilter != nullptr )
				return s_pPreviousFilter( pInfo );
			return EXCEPTION_EXECUTE_HANDLER;
		}
	} // namespace

	void CrashHandler::initialize()
	{
		if ( s_bInstalled.exchange( true ) )
			return;

		CallStackCapture::initialize();

		s_pPreviousFilter = SetUnhandledExceptionFilter( &onUnhandledException );
		SW_LOG_TRACE( "Crash handler installed." );
	}

	void CrashHandler::shutdown()
	{
		if ( s_bInstalled.exchange( false ) == false )
			return;

		SetUnhandledExceptionFilter( s_pPreviousFilter );
		s_pPreviousFilter = nullptr;

		// initialize 에서 잡은 심볼 참조를 돌려준다(참조 카운트 짝 맞추기).
		CallStackCapture::shutdown();
	}
} // namespace sw

#endif
