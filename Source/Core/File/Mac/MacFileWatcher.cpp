#include "pch.h"

#include "Core/File/Mac/MacFileWatcher.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#if defined( SW_PLATFORM_MACOS )
	#include <CoreServices/CoreServices.h>

namespace sw
{
	MacFileWatcher::MacFileWatcher() = default;

	MacFileWatcher::~MacFileWatcher()
	{
		stopWatching();
	}

	void MacFileWatcher::streamCallback( const void* /*pStreamRef*/, void* pClientCallBackInfo, size_t numEvents, void* pEventPaths,
										 const uint32* pEventFlags, const uint64* /*pEventIds*/ )
	{
		MacFileWatcher* pSelf = static_cast<MacFileWatcher*>( pClientCallBackInfo );
		if ( pSelf != nullptr )
			pSelf->handlePaths( numEvents, pEventPaths, pEventFlags );
	}

	void MacFileWatcher::handlePaths( size_t numEvents, void* pEventPaths, const uint32* pFlags )
	{
		utf8** ppPaths = static_cast<utf8**>( pEventPaths );
		for ( size_t eventIndex = 0; eventIndex < numEvents; ++eventIndex )
		{
			if ( ppPaths == nullptr || ppPaths[eventIndex] == nullptr )
				continue;

			const string fullPath = FileUtil::normalizeSeparators( ppPaths[eventIndex] );
			const string dir	  = FileUtil::getDirectoryPart( fullPath );
			const string name	  = FileUtil::getFileNamePart( fullPath );

			if ( _bRecursive == false && dir != _directoryPath )
				continue;

			FileWatcherAction action = FileWatcherAction::Modified;
			const uint32	  flag	 = pFlags != nullptr ? pFlags[eventIndex] : 0;
			if ( flag & kFSEventStreamEventFlagItemRemoved )
				action = FileWatcherAction::Removed;
			else if ( flag & kFSEventStreamEventFlagItemCreated )
				action = FileWatcherAction::Added;
			else if ( flag & kFSEventStreamEventFlagItemRenamed )
				action = FileWatcherAction::RenamedNewName;

			pushEvent( action, dir, name );
		}
	}

	bool MacFileWatcher::startWatching( string_view directoryPath, bool bRecursive )
	{
		if ( _bIsWatching )
			return false;

		if ( FileUtil::directoryExists( directoryPath ) == false )
		{
			SW_LOG_ERROR( "[MacFileWatcher] Directory does not exist: %#", string{ directoryPath }.c_str() );
			return false;
		}

		_directoryPath = FileUtil::normalizeSeparators( string{ directoryPath } );
		_bRecursive	   = bRecursive;

		CFStringRef pathRef = CFStringCreateWithCString( kCFAllocatorDefault, _directoryPath.c_str(), kCFStringEncodingUTF8 );
		if ( pathRef == nullptr )
			return false;

		CFArrayRef pathsToWatch = CFArrayCreate( kCFAllocatorDefault, reinterpret_cast<const void**>( &pathRef ), 1, &kCFTypeArrayCallBacks );
		CFRelease( pathRef );
		if ( pathsToWatch == nullptr )
			return false;

		FSEventStreamContext context{};
		context.info = this;

		const FSEventStreamCreateFlags createFlags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

		FSEventStreamRef stream = FSEventStreamCreate( kCFAllocatorDefault, reinterpret_cast<FSEventStreamCallback>( &MacFileWatcher::streamCallback ),
													   &context, pathsToWatch, kFSEventStreamEventIdSinceNow, 0.25, createFlags );
		CFRelease( pathsToWatch );
		if ( stream == nullptr )
		{
			SW_LOG_ERROR( "[MacFileWatcher] FSEventStreamCreate failed" );
			return false;
		}

		_pStream	  = stream;
		_bIsWatching  = true;
		_workerThread = std::thread( &MacFileWatcher::workerThreadMain, this );
		return true;
	}

	void MacFileWatcher::workerThreadMain()
	{
		FSEventStreamRef stream = static_cast<FSEventStreamRef>( _pStream );
		if ( stream == nullptr )
			return;

		CFRunLoopRef runLoop = CFRunLoopGetCurrent();
		_pRunLoop			 = runLoop;
		FSEventStreamScheduleWithRunLoop( stream, runLoop, kCFRunLoopDefaultMode );
		if ( FSEventStreamStart( stream ) == false )
		{
			SW_LOG_ERROR( "[MacFileWatcher] FSEventStreamStart failed" );
			_bIsWatching = false;
			return;
		}

		while ( _bIsWatching )
			CFRunLoopRunInMode( kCFRunLoopDefaultMode, 0.5, true );

		FSEventStreamStop( stream );
		FSEventStreamInvalidate( stream );
	}

	uint32 MacFileWatcher::pollEvents( vector<FileChangeEvent>& outEvents )
	{
		std::scoped_lock<mutex> lock{ _eventMutex };
		const uint32			count = static_cast<uint32>( _listEventQueue.size() );
		if ( count > 0 )
		{
			outEvents.insert( outEvents.end(), _listEventQueue.begin(), _listEventQueue.end() );
			_listEventQueue.clear();
		}
		return count;
	}

	void MacFileWatcher::stopWatching()
	{
		if ( _bIsWatching == false && _workerThread.joinable() == false )
			return;

		_bIsWatching = false;
		if ( _pRunLoop != nullptr )
			CFRunLoopStop( static_cast<CFRunLoopRef>( _pRunLoop ) );

		if ( _workerThread.joinable() )
			_workerThread.join();

		if ( _pStream != nullptr )
		{
			FSEventStreamRelease( static_cast<FSEventStreamRef>( _pStream ) );
			_pStream = nullptr;
		}
		_pRunLoop = nullptr;

		std::scoped_lock<mutex> lock{ _eventMutex };
		_listEventQueue.clear();
	}

	void MacFileWatcher::pushEvent( FileWatcherAction action, string_view absoluteDirectory, string_view name )
	{
		FileChangeEvent ev;
		ev._action	  = action;
		ev._directory = string{ absoluteDirectory };
		ev._filename  = string{ name };
		std::scoped_lock<mutex> lock{ _eventMutex };
		_listEventQueue.push_back( std::move( ev ) );
	}
} // namespace sw

#endif
