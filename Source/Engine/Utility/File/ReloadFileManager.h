/**
 * @file ReloadFileManager.h
 * @brief 런타임에 에셋 파일(텍스처, 셰이더 등)의 변경을 감지하고 등록된 구독자에게만 전달합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/IFileWatcher.h"

namespace sw
{
	using FileWatchMatchDelegate = Delegate<void( const FileChangeEvent& )>;

	/// @brief registerWatch가 돌려주는 감시 핸들
	struct FileWatchHandle
	{
		uint64 _id{ 0 };
		bool   isValid() const { return _id != 0; }
		bool   operator==( const FileWatchHandle& rhs ) const { return _id == rhs._id; }
		bool   operator!=( const FileWatchHandle& rhs ) const { return _id != rhs._id; }
	};

	/**
	 * @class ReloadFileManager
	 * @brief FileWatcher로 리소스 변경을 폴링하고, 등록된 path prefix + 확장자 매칭 시에만 콜백을 호출합니다.
	 * @note Windows: ReadDirectoryChangesW. Linux: inotify. Other platforms: mtime poll fallback.
	 */
	class SW_API ReloadFileManager
	{
	public:
		/** @brief 워치 목록을 비운 채 시작합니다. */
		ReloadFileManager();
		/** @brief 워처와 등록된 watch를 정리합니다. */
		~ReloadFileManager();

		/** @brief 초기화합니다. */
		bool initialize();
		/** @brief 종료합니다. */
		void shutdown();

		/**
		 * @brief 매 프레임 업데이트하여 파일 변경 사항을 폴링합니다.
		 */
		void update();

		/**
		 * @brief pathPrefix 하위 + extensions 매칭 시에만 onMatch 호출.
		 * @param pathPrefix 감시/필터 경로 (정규화됨)
		 * @param listExtension 예: { ".hlsl", ".hlsli" } (점 포함, 대소문자 무시)
		 */
		FileWatchHandle registerWatch( string_view pathPrefix, const vector<string>& listExtension, const FileWatchMatchDelegate& onMatch );
		/** @brief 워치 등록을 해제합니다. */
		void unregisterWatch( FileWatchHandle handle );

		/** @brief 레거시: 모든 매칭 구독 전 브로드캐스트용 (등록 watch와 별개로 유지하지 않음 — 매칭된 이벤트만) */
		FileChangeMulticastDelegate& getOnFileChangedEvent() { return _onFileChanged; }

	private:
		/// @brief path prefix + 확장자 필터 + 콜백
		struct WatchEntry
		{
			FileWatchHandle		   _handle;
			string				   _pathPrefix;
			vector<string>		   _listExtension;
			FileWatchMatchDelegate _onMatch;
		};

		/** @brief 이벤트가 이 watch의 prefix/확장자와 맞으면 true. */
		bool matchesWatch( const WatchEntry& entry, const FileChangeEvent& ev ) const;
		/** @brief 파일 변경 이벤트를 워치 콜백으로 보냅니다. */
		void dispatchEvents( const vector<FileChangeEvent>& listEvent );
		/** @brief mtime 폴백으로 변경을 모읍니다. */
		void pollMtimeFallback( vector<FileChangeEvent>& outListEvent );
		/** @brief 파일 확장자가 watch 허용 목록에 있으면 true. */
		bool extensionAllowed( const WatchEntry& entry, string_view filename ) const;

		unique_ptr<IFileWatcher>	  _fileWatcher;
		FileChangeMulticastDelegate	  _onFileChanged;
		vector<WatchEntry>			  _listWatch;
		uint64						  _nextWatchId{ 1 };
		unordered_map<string, uint64> _mapPollMtime;
		bool						  _bUseMtimePoll{ false };
	};
} // namespace sw
