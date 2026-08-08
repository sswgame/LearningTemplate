#pragma once

#include "Core/Utility/File/IFileWatcher.h"
#include "Core/Common/CommonHeaders.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
	/**
	 * @class LinuxFileWatcher
	 * @brief Linux 플랫폼 특화 FileWatcher (inotify + eventfd)
	 */
	class SW_API LinuxFileWatcher final : public IFileWatcher
	{
	public:
		LinuxFileWatcher();
		~LinuxFileWatcher() override;

		bool   startWatching( const std::string_view directoryPath, bool bRecursive = true ) override;
		uint32 pollEvents( std::vector<FileChangeEvent>& outEvents ) override;
		void   stopWatching() override;
		bool   isWatching() const override { return _bIsWatching; }

	private:
		void workerThreadMain();
		bool addWatchRecursive( const std::string& directoryPath );
		bool addWatchDirectory( const std::string& directoryPath );
		void removeWatch( int watchDescriptor );
		void pushEvent( FileWatcherAction action, const std::string& absoluteDirectory, const std::string& name );

	private:
		int				  _inotifyFd = -1;
		int				  _wakeFd	 = -1;
		std::atomic<bool> _bIsWatching{ false };
		bool			  _bRecursive = true;
		std::string		  _directoryPath;

		std::thread					 _workerThread;
		std::mutex					 _eventMutex;
		std::mutex					 _watchMutex;
		std::vector<FileChangeEvent> _eventQueue;
		std::unordered_map<int, std::string> _watchDescriptorToPath;
	};
} // namespace sw

#endif // SW_PLATFORM_LINUX
