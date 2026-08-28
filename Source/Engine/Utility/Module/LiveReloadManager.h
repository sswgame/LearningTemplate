/**
 * @file LiveReloadManager.h
 * @brief 모듈 공유 라이브러리 섀도 복사 기반 핫 리로드 (+ 의존 캐스케이드)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventDispatcher.h"

#include "Engine/Common/Common.h"

namespace sw
{
	struct ComponentFactoryRegistrar;
} // namespace sw

struct TypeRegistrar;
struct EnumRegistrar;

namespace sw
{
	class IFileWatcher;
	/**
	 * @brief 모듈을 섀도 경로에 복사해 로드하는 핫 리로드 매니저
	 * @note 캐스케이드는 의존 모듈을 먼저 교체(위상 정렬). 전부 prepare 성공 후에만 commit.
	 *       언로드는 위상 역순(dependent 먼저). keep-old: prepare 실패 시 새 이미지는 버리고 기존 핸들 유지.
	 *       commit 중 실패하거나 onAfter가 그래프를 poison하면 나머지 commit을 중단한다.
	 *       이미 교체된 DLL은 되돌릴 수 없음.
	 */
	class SW_API LiveReloadManager
	{
	public:
		using OnBeforeReloadDelegate	  = Delegate<void()>;
		using OnAfterReloadDelegate		  = Delegate<void( void* pLibraryModule )>;
		using OnBeforeCommitBatchDelegate = Delegate<void( const vector<string>& listModuleNames )>;
		using DrainWorkersDelegate		  = Delegate<void()>;

		/** @brief 모듈 맵과 워처를 비운 채 시작합니다. */
		LiveReloadManager();
		/** @brief 로드된 모듈을 언로드합니다. */
		~LiveReloadManager();

		/** @brief 복사를 금지합니다. */
		LiveReloadManager( const LiveReloadManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		LiveReloadManager& operator=( const LiveReloadManager& ) = delete;

		/** @brief 라이브 리로드 매니저를 종료하고 모든 모듈을 해제합니다. */
		void shutdown();

		/**
		 * @brief 모듈을 등록하고 섀도 로드합니다.
		 * @param moduleName DLL/SO basename (확장자 없음)
		 * @param listDependsOn 이 모듈이 의존하는 모듈 이름 (먼저 로드됨)
		 */
		bool registerModule( string_view moduleName, const vector<string>& listDependsOn = {} );

		/** @brief 해당 모듈(+종속) 리로드를 예약합니다. */
		void triggerReload( string_view moduleName );

		/** @brief 파일 변경 사항을 모니터링하고 예약된 리로드를 수행합니다. */
		void update();

		/** @brief 모듈이 리로드되기 직전에 호출될 델리게이트를 설정합니다. */
		void setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate );

		/** @brief 모듈이 리로드된 직후에 호출될 델리게이트를 설정합니다. */
		void setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate );

		/**
		 * @brief 캐스케이드가 전부 prepare된 뒤, 첫 commit 직전에 한 번 호출됩니다.
		 * @details 키트 DLL unload 전에 SWGame을 내릴 때 사용합니다.
		 */
		void setOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate );

		/**
		 * @brief 모듈 언로드 직전에 호출되어 워커 스레드를 배수합니다.
		 * @details TaskManager 만으로는 부족합니다. RenderThread 는 App 소유라 Engine 에서 직접 볼 수 없으므로,
		 *          App 이 renderThread->waitIdle() / device.waitIdle() 을 여기에 연결해야
		 *          present 훅이 실행 중인 이미지를 FreeLibrary 하지 않습니다.
		 */
		void setDrainWorkers( DrainWorkersDelegate delegate );

		/** @brief 혼합 DLL 그래프로 간주하고 이후 리로드를 막습니다. 프로세스 재시작이 필요합니다. */
		void markGraphBroken( string_view reason );
		/** @brief commit 실패·사이클·바인딩 실패로 그래프가 깨졌으면 true. */
		bool isGraphBroken() const { return _bReloadGraphBroken; }

		/** @brief 현재 핫리로드 Batch 처리 중인지 여부를 반환합니다. */
		bool isReloadingBatch() const { return _bReloadingBatch; }

		/** @brief 로드된 모듈의 핸들을 반환합니다. */
		void* getModuleHandle( string_view moduleName ) const;

		/** @brief 리로드 시 자동 해제를 위해 EventSubscription을 등록합니다. */
		void addEventSubscription( string_view moduleName, const EventDispatcher::EventSubscription& token );

	private:
		struct ModuleContext;

		struct PreparedShadow
		{
			void*						   _pHandle{ nullptr };
			string						   _tempPath;
			uint64						   _sourceMtime{ 0 };
			TypeRegistrar*				   _pTypeHead{ nullptr };
			EnumRegistrar*				   _pEnumHead{ nullptr };
			sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
		};

		/** @brief 섀도 복사본을 LoadLibrary 합니다. */
		bool loadShadowCopyModule( ModuleContext& ctx );
		/** @brief 섀도 복사본을 만들고 로드만 합니다 (아직 교체 안 함). */
		bool prepareShadowCopy( ModuleContext& ctx, PreparedShadow& out );
		/** @brief 섀도 핸들로 교체하고 콜백을 호출합니다. */
		bool commitShadowCopy( ModuleContext& ctx, PreparedShadow& prepared );
		/** @brief prepare 실패 시 새 이미지를 버립니다. */
		void abortShadowCopy( PreparedShadow& prepared );
		/** @brief 모듈 핸들을 언로드합니다. */
		void unloadModule( ModuleContext& ctx );
		/** @brief 언로드 전 해당 모듈 태스크가 끝나길 기다립니다. */
		void drainTasksBeforeUnload();
		/** @brief root와 종속 모듈 이름을 중복 없이 모읍니다. */
		void collectDependentClosure( string_view root, vector<string>& listOutUnique ) const;
		/** @brief 의존 순으로 위상 정렬합니다. 사이클이면 false. */
		bool topoSortSubgraph( const vector<string>& listNames, vector<string>& listOutOrdered ) const;
		/** @brief 부분 그래프를 prepare 전부 성공한 뒤에만 commit합니다. */
		void reloadCascade( const vector<string>& listSubgraphNames );

		/// @brief 등록된 모듈: 경로, 핸들, 의존, 리로드 예약
		struct ModuleContext
		{
			OnBeforeReloadDelegate					   _onBeforeReload;
			OnAfterReloadDelegate					   _onAfterReload;
			string									   _moduleName;
			string									   _originalModulePath;
			string									   _tempModulePath;
			vector<string>							   _listDependsOn;
			vector<EventDispatcher::EventSubscription> _listEventSubscriptions;
			void*									   _pLibraryModule;
			uint64									   _loadedSourceMtime;
			uint64									   _debounceMtime;
			std::chrono::steady_clock::time_point	   _debounceSince;
			std::atomic<bool>						   _bPendingReload;
			std::atomic<bool>						   _bMtimeDebouncing;
			std::atomic<bool>						   _bForceReload;

			/** @brief 원자 플래그 끈 기본값. */
			ModuleContext() noexcept;
			/** @brief 핸들과 경로를 이동합니다. */
			ModuleContext( ModuleContext&& other ) noexcept;
			/** @brief 이동 대입입니다. */
			ModuleContext& operator=( ModuleContext&& other ) noexcept;
		};

		static constexpr int32 kMtimeDebounceMs = 300;

		unordered_map<string, ModuleContext> _mapModule;
		unique_ptr<IFileWatcher>			 _fileWatcher;
		OnBeforeCommitBatchDelegate			 _onBeforeCommitBatch;
		DrainWorkersDelegate				 _drainWorkers;
		bool								 _bReloadGraphBroken;
		bool								 _bReloadingBatch;
	};

	// ------------------------------------------------------------------------------
	// Delay-load 훅 — App이 매니저를 연결, Windows delay-load가 섀도 핸들을 조회
	// ------------------------------------------------------------------------------
	/** @brief App이 LiveReloadManager를 연결합니다. */
	SW_API void setDelayLoadLiveReloadManager( LiveReloadManager* pManager );
	/** @brief delay-load 훅이 조회할 LiveReloadManager를 반환합니다. */
	SW_API LiveReloadManager* getDelayLoadLiveReloadManager();
} // namespace sw
