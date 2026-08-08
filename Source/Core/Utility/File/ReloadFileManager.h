#pragma once
/**
 * @file ReloadFileManager.h
 * @brief 런타임에 에셋 파일(텍스처, 셰이더 등)의 변경을 감지하고 등록된 구독자에게만 전달합니다.
 */

#include "Core/Common/CommonDefines.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/File/IFileWatcher.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	using FileWatchMatchDelegate = Delegate<void( const FileChangeEvent& )>;

	struct FileWatchHandle
	{
		uint64 _id = 0;
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
		ReloadFileManager();
		~ReloadFileManager();

		bool initialize();
		void shutdown();

		/**
		 * @brief 매 프레임 업데이트하여 파일 변경 사항을 폴링합니다.
		 */
		void update();

		/**
		 * @brief pathPrefix 하위 + extensions 매칭 시에만 onMatch 호출.
		 * @param pathPrefix 감시/필터 경로 (정규화됨)
		 * @param extensions 예: { ".hlsl", ".hlsli" } (점 포함, 대소문자 무시)
		 */
		FileWatchHandle registerWatch( const std::string& pathPrefix, const std::vector<std::string>& extensions, const FileWatchMatchDelegate& onMatch );
		void			unregisterWatch( FileWatchHandle handle );

		/** @brief 레거시: 모든 매칭 구독 전 브로드캐스트용 (등록 watch와 별개로 유지하지 않음 — 매칭된 이벤트만) */
		FileChangeMulticastDelegate& getOnFileChangedEvent() { return _onFileChanged; }

	private:
		struct WatchEntry
		{
			FileWatchHandle			 _handle;
			std::string				 _pathPrefix;
			std::vector<std::string> _extensions;
			FileWatchMatchDelegate	 _onMatch;
		};

		bool			   matchesWatch( const WatchEntry& entry, const FileChangeEvent& ev ) const;
		static std::string extractExtension( const std::string& filename );
		void			   dispatchEvents( const std::vector<FileChangeEvent>& events );
		void			   pollMtimeFallback( std::vector<FileChangeEvent>& outEvents );
		bool			   extensionAllowed( const WatchEntry& entry, const std::string& filename ) const;

		std::unique_ptr<IFileWatcher>					   _fileWatcher;
		FileChangeMulticastDelegate						   _onFileChanged;
		std::vector<WatchEntry>							   _watches;
		uint64											   _nextWatchId = 1;
		std::unordered_map<std::string, std::filesystem::file_time_type> _pollMtimes;
		bool											   _bUseMtimePoll = false;
	};
} // namespace sw
