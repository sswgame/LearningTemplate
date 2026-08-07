#pragma once

#include "Core/Utility/File/IFileWatcher.h"
#include "Core/Common/CommonHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

// Forward declare HANDLE to avoid full Windows.h inclusion in header
using HANDLE = void*;

namespace sw
{
	/**
	 * @class WindowsFileWatcher
	 * @brief Windows 플랫폼 특화 FileWatcher (ReadDirectoryChangesW 사용)
	 */
	class SW_API WindowsFileWatcher final : public IFileWatcher
	{
	public:
		WindowsFileWatcher();
		virtual ~WindowsFileWatcher() override;

		virtual bool   startWatching( const std::string_view directoryPath, bool bRecursive = true ) override;
		virtual uint32 pollEvents( std::vector<FileChangeEvent>& outEvents ) override;
		virtual void   stopWatching() override;
		virtual bool   isWatching() const override { return _bIsWatching; }

	private:
		void workerThreadMain();

	private:
		HANDLE			  _hDirectory  = nullptr;
		std::atomic<bool> _bIsWatching = false;
		bool			  _bRecursive  = true;
		std::string		  _directoryPath;

		std::thread					 _workerThread;
		std::mutex					 _eventMutex;
		std::vector<FileChangeEvent> _eventQueue;

		HANDLE _hCompletionPort = nullptr;
	};
} // namespace sw

#endif // SW_PLATFORM_WINDOWS
