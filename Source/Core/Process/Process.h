/**
 * @file Process.h
 * @brief 크로스 플랫폼 외부 프로세스 생성, 출력 스트리밍, 제어 및 종료 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
	/** @brief 프로세스 실시간 표준 출력/에러 라인 콜백 */
	using ProcessOutputDelegate = Delegate<void( string_view line, bool bIsStdErr )>;

	/**
	 * @struct ProcessOptions
	 * @brief 프로세스 생성 옵션
	 */
	struct ProcessOptions
	{
		string _workingDirectory{};
		bool   _bCreateWindow{ false };
	};

	/**
	 * @class Process
	 * @brief 외부 OS 프로세스를 생성하고 표준 출력을 파이프로 스트리밍하며, 종료 대기 및 강제 종료를 지원하는 크로스 플랫폼 클래스
	 */
	class SW_API Process
	{
	public:
		/** @brief 비어 있는 프로세스 핸들로 생성합니다. */
		Process();
		/** @brief 프로세스 핸들을 닫고 리소스를 정리합니다 (실행 중인 프로세스는 분리됨). */
		~Process();

		Process( const Process& )			 = delete;
		Process& operator=( const Process& ) = delete;

		Process( Process&& other ) noexcept;
		Process& operator=( Process&& other ) noexcept;

		/**
		 * @brief 자식 프로세스를 비동기로 실행하고 입출력 파이프를 연결합니다.
		 * @param command 실행할 명령어 (인자 포함)
		 * @param options 작업 디렉터리 등 프로세스 옵션
		 * @return 프로세스 생성 성공 시 true
		 */
		bool launch( string_view command, const ProcessOptions& options = {} );

		/**
		 * @brief 파이프로부터 다음 출력 한 줄을 읽습니다.
		 * @param outLine 출력 문자열이 담길 버퍼
		 * @return 읽기 성공 시 true, EOF 또는 파이프 종료 시 false
		 */
		bool readOutputLine( string& outLine );

		/**
		 * @brief 프로세스 종료를 대기하고 종료 코드를 반환합니다.
		 * @return 프로세스 종료 코드 (-1 = 대기 실패)
		 */
		int32 waitForExit();

		/**
		 * @brief 프로세스를 강제 종료합니다.
		 * @param exitCode 종료 코드
		 * @return 종료 요청 성공 시 true
		 */
		bool terminate( int32 exitCode = 1 );

		/** @brief 프로세스가 아직 실행 중인지 여부를 반환합니다. */
		bool isRunning() const;

		/** @brief 프로세스 ID를 반환합니다. */
		int32 getProcessId() const { return _processId; }
		/** @brief 네이티브 프로세스 핸들을 반환합니다 (Windows: HANDLE, POSIX: pid 등). */
		void* getNativeHandle() const { return _pNativeHandle; }

		/**
		 * @brief 동기 실행 헬퍼: 프로세스를 실행하고 출력을 실시간 델리게이트로 전달하며 종료까지 대기합니다.
		 * @param command 실행할 명령어
		 * @param options 작업 디렉터리 등 프로세스 옵션
		 * @param onOutput 표준 출력/에러 라인 수신 델리게이트
		 * @return 프로세스 종료 코드 (-1 = 실행 실패)
		 */
		static int32 execute( string_view command, const ProcessOptions& options = {}, ProcessOutputDelegate onOutput = {} );

	private:
		void cleanup();

	private:
		void*  _pNativeHandle;
		void*  _pStdOutRead;
		void*  _pNativeThread;
		string _bufferedOutput;
		int32  _processId;
		bool   _bRunning;
	};
} // namespace sw
