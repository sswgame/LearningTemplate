/**
 * @file MacFileWatcher.h
 * @brief FSEvents 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/IFileWatcher.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
	/**
	 * @class MacFileWatcher
	 * @brief macOS FSEvents 파일 감시
	 */
	class SW_API MacFileWatcher final : public IFileWatcher
	{
	public:
		/** @brief 스트림과 큐를 비운 상태로 둡니다. */
		MacFileWatcher();
		/** @brief 감시를 멈추고 워커를 합류시킵니다. */
		~MacFileWatcher() override;

		/** @brief FSEventStream을 열고 런루프 워커를 띄웁니다. */
		bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
		/** @brief 워커 큐에서 이벤트를 꺼내 outEvents 에 담습니다. */
		uint32 pollEvents( vector<FileChangeEvent>& outEvents ) override;
		/** @brief 런루프를 멈추고 스트림을 해제합니다. */
		void stopWatching() override;
		/** @brief 워커가 돌고 있으면 true입니다. */
		bool isWatching() const override { return _bIsWatching; }

	private:
		/** @brief CFRunLoop에서 FSEvents를 돌립니다. */
		void workerThreadMain();
		/** @brief 뮤텍스 아래에서 변경 이벤트를 큐에 넣습니다. */
		void pushEvent( FileWatcherAction action, string_view absoluteDirectory, string_view name );
		/** @brief FSEvents 콜백에서 경로를 큐에 넣습니다. */
		void		handlePaths( size_t numEvents, void* pEventPaths, const uint32* pFlags );
		static void streamCallback( const void* pStreamRef, void* pClientCallBackInfo, size_t numEvents, void* pEventPaths,
									const uint32* pEventFlags, const uint64* pEventIds );

	private:
		void*		 _pStream{ nullptr };  ///< FSEventStreamRef
		void*		 _pRunLoop{ nullptr }; ///< CFRunLoopRef
		atomic<bool> _bIsWatching{ false };
		bool		 _bRecursive{ true };
		string		 _directoryPath;

		std::thread				_workerThread;
		mutex					_eventMutex;
		vector<FileChangeEvent> _listEventQueue;
	};
} // namespace sw

#endif
