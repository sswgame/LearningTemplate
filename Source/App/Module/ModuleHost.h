/**
 * @file ModuleHost.h
 * @brief 에디터/게임 모듈 라이프사이클 관리 — LiveReload 콜백·API 바인딩
 *
 * @note RuntimeAPI 레이어에 위치하여 호스트↔모듈 계약의 라이프사이클을 관리합니다.
 *       Engine은 Editor를 모르고, App은 EditorAPI/GameAPI 세부사항을 모릅니다.
 *       Dev 모드에서 LiveReloadManager와 협력해 핫리로드 전후 콜백을 처리합니다.
 */
#pragma once
#include "App/AppConfig.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/GameAPI.h"

namespace sw
{
	struct GameKitConfig;
	class IWindow;
	class IRHIDevice;
	class RHI;
	class LiveReloadManager;
	class RenderThread;
	struct NativeWindowEvent;

	/**
	 * @class ModuleHost
	 * @brief 에디터·게임 모듈의 생명주기·API 바인딩을 캡슐화합니다.
	 *
	 * @details
	 *   App은 ModuleHost를 unique_ptr으로 소유합니다.
	 *   - initialize()에서 LiveReloadManager에 콜백을 등록합니다.
	 *   - 핫리로드 시 onBefore/onAfter 콜백이 자동으로 호출됩니다.
	 */
	class ModuleHost
	{
	public:
		// 1) ctor/dtor → initialize/shutdown
		ModuleHost();
		~ModuleHost();

		ModuleHost( const ModuleHost& )			   = delete;
		ModuleHost& operator=( const ModuleHost& ) = delete;

		/**
		 * @brief LiveReloadManager에 콜백을 등록하고 에디터/게임 모듈을 로드합니다.
		 * @param pLiveReloadManager Dev 모드 전용 모듈 매니저 (nullable for Shipping)
		 * @param pRHI 활성 RHI
		 * @param pWindow 플랫폼 윈도우
		 * @param pRenderThread 렌더 스레드 (drainWorkers 용)
		 * @param bEnableEditor 에디터 모드 여부
		 */
		bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& gameKitModuleList );
		void shutdown();

		// 3) 프레임 단위 처리
		/** @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */
		void updateEditorUI( float32 deltaTime );
		/** @brief 게임 업데이트를 호출합니다. */
		void updateGame( float32 deltaTime );
		/** @brief 물리 등 고정 주기 게임 업데이트를 호출합니다. */
		void fixedUpdateGame( float32 fixedDeltaTime );
		/** @brief 네이티브 윈도우 이벤트를 에디터에 전달합니다. */
		bool onWindowMessage( const NativeWindowEvent& event );

		/** @brief 이번 프레임 Game View RT 핸들과 크기를 에디터에서 조회합니다. */
		void getGameViewport( uint64& renderTarget, uint32& width, uint32& height ) const;

		/** @brief 에디터 인스턴스 핸들을 반환합니다. */
		EditorHandle getEditor() const { return _editor; }
		/** @brief 게임 인스턴스 핸들을 반환합니다. */
		GameHandle getGame() const { return _game; }
		/** @brief EditorAPI 테이블을 반환합니다. */
		const EditorAPI& getEditorAPI() const { return _editorApi; }
		/** @brief GameAPI 테이블을 반환합니다. */
		const GameAPI& getGameAPI() const { return _gameApi; }
		/** @brief 에디터 모드인지를 반환합니다. */
		bool isEditorEnabled() const { return _bEnableEditor; }

		// 4) LiveReload 콜백 — LiveReloadManager의 델리게이트가 호출
		void onBeforeEditorReload();
		void onAfterEditorReload( void* pLibraryModule );
		void onBeforeGameReload();
		void onAfterGameReload( void* pLibraryModule );
		void onBeforeGameplayDllReload();
		void onAfterGameplayDllReload( void* pLibraryModule );
		void onBeforeCommitBatch( const vector<string>& moduleNames );
		/** @brief RHI 백엔드 핫스왑 전 기존 에디터 및 게임 런타임 인스턴스를 안전하게 정리합니다. */
		void onBeforeRhiSwap();
		void drainRenderWorkers();
		void poisonLiveReload( const utf8* pReason );

		// 5) 모듈 바인딩
		bool bindEditorAPI( void* pLibraryModule );
		bool bindGameAPI( void* pLibraryModule );

		/** @brief 에디터·게임 모듈이 필수 리소스를 로드한 후 Splash를 내립니다. */
		void notifyModulesReady();

		/** @brief RHI 핫스왑 후 에디터/게임을 재초기화합니다. */
		bool reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule );

	private:
		EditorAPI	 _editorApi;
		GameAPI		 _gameApi;
		EditorHandle _editor;
		GameHandle	 _game;

		LiveReloadManager* _pLiveReloadManager; // non-owning
		RHI*			   _pRHI;				// non-owning
		IWindow*		   _pWindow;			// non-owning
		RenderThread*	   _pRenderThread;		// non-owning

		uint8				   _bEnableEditor : 1;
		[[maybe_unused]] uint8 _reserved	  : 7;

		// 핫리로드 시 게임 상태 보존용 재사용 버퍼
		uint8* _pGameSavedStateBuffer{ nullptr };
		size_t _gameSavedStateCapacity{ 0 };
		size_t _gameSavedStateSize{ 0 };
	};
} // namespace sw
