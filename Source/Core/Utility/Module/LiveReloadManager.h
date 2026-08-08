#pragma once
/**
 * @file LiveReloadManager.h
 * @brief 모듈 공유 라이브러리 섀도 복사 기반 핫 리로드
 */
#include "Core/Common/Common.h"

namespace sw
{
	/**
	 * @brief 모듈을 섀도 경로에 복사해 로드하는 핫 리로드 매니저
	 * @note Windows/macOS: `*_temp_N` 고유 섀도 (two-handle swap, Windows는 PDB 동반).
	 *       Linux: `*_live` 고정 섀도 (언로드 → 덮어쓰기 → 로드, 디버거 모듈 경로 안정화).
	 */
	class LiveReloadManager
	{
	public:
		using OnBeforeReloadDelegate = sw::Delegate<void()>;
		using OnAfterReloadDelegate	 = sw::Delegate<void( void* hLibraryModule )>;

		LiveReloadManager();
		~LiveReloadManager();

		/** @brief 로드된 모듈을 언로드하고 종료합니다. */
		void shutdown();

		/** @brief 모듈 이름을 등록하고 섀도 복사본을 로드합니다. */
		bool registerModule( const std::string& moduleName );
		/** @brief 다음 update에서 해당 모듈 리로드를 예약합니다. */
		void triggerReload( const std::string& moduleName );
		/** @brief 예약된 리로드를 수행합니다 (mtime 자동 리로드는 debounce). */
		void update();
		/** @brief 현재 로드된 모듈 핸들을 반환합니다. */
		void* getModuleHandle( const std::string& moduleName ) const;

		/** @brief 리로드 직전 콜백을 설정합니다. */
		void setOnBeforeReload( const std::string& moduleName, OnBeforeReloadDelegate delegate );
		/** @brief 리로드 직후 콜백을 설정합니다. */
		void setOnAfterReload( const std::string& moduleName, OnAfterReloadDelegate delegate );

	private:
		static constexpr int32 kMtimeDebounceMs = 300;

		struct ModuleContext
		{
			ModuleContext() noexcept;

			OnBeforeReloadDelegate _onBeforeReload;
			OnAfterReloadDelegate  _onAfterReload;
			std::string			   _moduleName;
			std::string			   _originalModulePath;
			std::string			   _tempModulePath;
			void*				   _hLibraryModule	  = nullptr;
			uint64				   _loadedSourceMtime = 0; ///< Timestamp of original module when current shadow was loaded
			uint64				   _debounceMtime	  = 0; ///< Last observed newer source mtime while debouncing
			std::chrono::steady_clock::time_point _debounceSince{};
			uint8				   _bPendingReload	 : 1;
			uint8				   _bMtimeDebouncing : 1;
			[[maybe_unused]] uint8 _reserved		 : 6;
		};

		/** @brief 원본 모듈을 플랫폼별 섀도 경로에 복사한 뒤 로드합니다. */
		bool loadShadowCopyModule( ModuleContext& ctx );

		std::unordered_map<std::string, ModuleContext> _modules;
	};
} // namespace sw
