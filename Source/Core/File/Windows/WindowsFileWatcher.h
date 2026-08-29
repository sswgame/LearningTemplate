/**
 * @file WindowsFileWatcher.h
 * @brief ReadDirectoryChangesW + IOCP 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/IFileWatcher.h"

#if defined( SW_PLATFORM_WINDOWS )

// Windows.h 전체를 헤더에 넣지 않도록 HANDLE만 전방 선언합니다.
using HANDLE = void*;

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) WindowsFileWatcher — IOCP 완료를 워커가 큐에 넣고 pollEvents 가 꺼냄
	// ------------------------------------------------------------------------------
	/**
	 * @class WindowsFileWatcher
	 * @brief Windows 플랫폼 특화 FileWatcher (ReadDirectoryChangesW 사용)
	 */
	class SW_API WindowsFileWatcher final : public IFileWatcher
	{
	public:
		/** @brief 핸들과 큐를 비운 상태로 둡니다. */
		WindowsFileWatcher();
		/** @brief 감시를 멈추고 워커를 합류시킵니다. */
		virtual ~WindowsFileWatcher() override;

		/** @brief 디렉터리 핸들·IOCP를 열고 워커를 띄웁니다. */
		bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
		/** @brief 워커 큐에서 이벤트를 꺼내 outEvents 에 담습니다. */
		uint32 pollEvents( vector<FileChangeEvent>& outEvents ) override;
		/** @brief IOCP를 깨우고 워커를 멈춘 뒤 핸들을 닫습니다. */
		void stopWatching() override;
		/** @brief 워커가 돌고 있으면 true입니다. */
		bool isWatching() const override { return _bIsWatching; }

	private:
		/** @brief ReadDirectoryChangesW 완료를 이벤트 큐에 넣습니다. */
		void workerThreadMain();

		HANDLE					_hDirectory;
		atomic<bool>			_bIsWatching;
		bool					_bRecursive;
		string					_directoryPath;
		std::thread				_workerThread;
		mutex					_eventMutex;
		vector<FileChangeEvent> _listEventQueue;
		HANDLE					_hCompletionPort;
	};

} // namespace sw

#endif // SW_PLATFORM_WINDOWS
