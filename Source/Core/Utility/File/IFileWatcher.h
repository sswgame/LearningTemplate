#pragma once

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{
	enum class FileWatcherAction : uint8
	{
		Added,
		Removed,
		Modified,
		RenamedOldName,
		RenamedNewName
	};

	struct FileChangeEvent
	{
		FileWatcherAction _action;
		std::string		  _directory;
		std::string		  _filename;
	};

	SW_DECLARE_DELEGATE( void, FileChangeDelegate, const FileChangeEvent& );
	SW_DECLARE_MULTI_CAST_DELEGATE( void, FileChangeMulticastDelegate, const FileChangeEvent& );

	/**
	 * @class IFileWatcher
	 * @brief 디렉토리 및 파일 변경 이벤트를 모니터링하는 크로스플랫폼 인터페이스
	 */
	class SW_API IFileWatcher
	{
	public:
		virtual ~IFileWatcher() = default;

		IFileWatcher( const IFileWatcher& ) = delete;
		IFileWatcher& operator=( const IFileWatcher& ) = delete;

		/**
		 * @brief 특정 디렉토리의 모니터링을 시작합니다.
		 * @param directoryPath 감시할 디렉토리의 절대/상대 경로
		 * @param bRecursive 하위 폴더도 포함하여 감시할지 여부
		 * @return 모니터링 시작 성공 여부
		 */
		virtual bool startWatching( const std::string_view directoryPath, bool bRecursive = true ) = 0;

		/**
		 * @brief 파일 변경 이벤트를 처리하기 위해 큐를 폴링합니다. (메인 스레드의 업데이트 루프에서 호출 권장)
		 * @param outEvents 발생한 파일 변경 이벤트 목록
		 * @return 발생한 이벤트 개수
		 */
		virtual uint32 pollEvents( std::vector<FileChangeEvent>& outEvents ) = 0;

		/**
		 * @brief 모니터링을 종료하고 리소스를 해제합니다.
		 */
		virtual void stopWatching() = 0;

		/** @brief 현재 모니터링 중인지 여부 반환 */
		virtual bool isWatching() const = 0;

	protected:
		IFileWatcher() = default;
	};
}
