/**
 * @file EditorWorkspace.h
 * @brief 에디터 선택 / 애셋 포커스 / 기즈모 / 윈도우 열기 허브 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Uuid/Uuid.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
	class Component;
} // namespace sw

namespace sw::editor
{
	/** @brief 인스펙터가 보여주는 대상 */
	enum class InspectMode : uint8
	{
		GameObject = 0,
		Asset
	};

	/** @brief 에디터 뷰포트 카메라 북마크 (위치, 회전, 타깃) */
	struct CameraBookmark
	{
		string	_name;
		float3	_position{ 0.0f, 0.0f, 0.0f };
		float3	_rotation{ 0.0f, 0.0f, 0.0f };
		float3	_orbitTarget{ 0.0f, 0.0f, 0.0f };
		float32 _orbitDistance{ 5.0f };
		bool	_bValid{ false };
	};

	/** @brief Isolation에서 숨긴 오브젝트와 이전 활성 상태 */
	struct PrefabIsolationHiddenObject
	{
		uint64 _objectId{ 0 };
		uint8  _bWasActive{ SW_FALSE };
	};

	/** @brief 한 단계의 인플레이스 프리팹 Isolation */
	struct PrefabIsolationFrame
	{
		string								_prefabPath;
		vector<PrefabIsolationHiddenObject> _listHidden;
		uint64								_rootObjectId{ 0 };
		uint8								_bSpawnedRoot{ SW_FALSE };
	};

	/**
	 * @class EditorWorkspace
	 * @brief 에디터의 전역 작업 공간 상태(선택, 포커스 애셋, 기즈모, 윈도우 요청)를 총괄하는 클래스 (EditorContext 소유)
	 */
	class EditorWorkspace
	{
	public:
		EditorWorkspace();
		~EditorWorkspace() = default;

		// ------------------------------------------------------------------------------
		// 1) 선택 — 오브젝트 / 컴포넌트
		// ------------------------------------------------------------------------------
		uint64		  getSelectedObjectId() const;
		GameObjectPtr getSelectedObject() const;
		uint64		  getSelectedComponentId() const { return _selectedComponentId; }
		void		  setSelectedComponentId( uint64 id ) { _selectedComponentId = id; }
		string		  getSelectedObjectName() const;
		const string& getSelectedComponentKey() const { return _selectedComponentKey; }
		void		  setSelectedComponentKey( string_view key ) { _selectedComponentKey = key; }
		void		  clearSelection();

		void selectGameObject( GameObjectPtr pObj, SelectionMode mode = SelectionMode::Replace );
		void selectComponent( GameObjectPtr pObj, ComponentPtr pComp );
		void remapSelectionByObjectName( GameObjectManager* pGameObjectManager );

		// ------------------------------------------------------------------------------
		// 2) 애셋 포커스 · 인스펙트 모드
		// ------------------------------------------------------------------------------
		const string& getFocusedAssetPath() const { return _focusedAssetPath; }
		void		  setFocusedAssetPath( const utf8* pPath );

		InspectMode getInspectMode() const { return _inspectMode; }
		void		setInspectMode( InspectMode mode ) { _inspectMode = mode; }

		// ------------------------------------------------------------------------------
		// 3) 기즈모 — 조작 모드 / 로컬 스페이스
		// ------------------------------------------------------------------------------
		int32 getGizmoOperation() const { return _gizmoOperation; }
		void  setGizmoOperation( int32 op ) { _gizmoOperation = op; }

		bool isGizmoLocalSpace() const { return _bGizmoLocalSpace == SW_TRUE; }
		void setGizmoLocalSpace( bool bLocal ) { _bGizmoLocalSpace = ( bLocal ) ? SW_TRUE : SW_FALSE; }

		// ------------------------------------------------------------------------------
		// 4) 윈도우 열기 요청 — 메뉴가 쓰고 셸이 consume
		// ------------------------------------------------------------------------------
		const string& getPendingOpenPanelTitle() const { return _pendingOpenPanelTitle; }
		void		  requestOpenPanel( const utf8* pTitle );
		bool		  consumeOpenPanel( string& outTitle );

		// ------------------------------------------------------------------------------
		// 5) 씬 열기 — FileDialog는 백그라운드, consume은 메인 스레드
		// ------------------------------------------------------------------------------
		void requestLoadScene( string_view path );
		bool consumeLoadScene( string& outPath );

		/** @brief 미저장 확인 뒤에 이어서 할 씬 동작을 넣습니다. */
		void					 setPendingSceneAction( EditorPendingSceneAction action, string_view loadPath = {} );
		EditorPendingSceneAction getPendingSceneAction() const { return _pendingSceneAction; }
		const string&			 getPendingSceneActionPath() const { return _pendingSceneActionPath; }
		void					 clearPendingSceneAction();

		/** @brief 마지막으로 동기화한 씬 세대입니다. */
		uint64 getObservedSceneGeneration() const { return _observedSceneGeneration; }
		void   setObservedSceneGeneration( uint64 generation ) { _observedSceneGeneration = generation; }

		/** @brief 활성 씬 오브젝트에서 프리팹 맵을 다시 채웁니다. */
		void rebuildGameObjectPrefabMap( GameObjectManager* pManager );
		/** @brief 프리팹 맵과 선택을 지웁니다. */
		void clearGameObjectPrefabMap();

		/** @brief 활성 씬에 저장되지 않은 에디터 변경이 있음을 표시합니다. */
		void markSceneDirty() { _bSceneDirty = SW_TRUE; }
		/** @brief 씬 dirty 플래그를 지웁니다. 저장/로드 성공 시 호출합니다. */
		void clearSceneDirty() { _bSceneDirty = SW_FALSE; }
		/** @brief 저장하지 않은 씬 변경이 있으면 true입니다. */
		bool isSceneDirty() const { return _bSceneDirty == SW_TRUE; }

		// ------------------------------------------------------------------------------
		// 6) 스크롤 타깃 · 본 계층 팝업
		// ------------------------------------------------------------------------------
		uint64 getScrollToComponentId() const { return _scrollToComponentId; }
		void   setScrollToComponentId( uint64 id ) { _scrollToComponentId = id; }

		uint64 getScrollToObjectId() const { return _scrollToObjectId; }
		void   setScrollToObjectId( uint64 id ) { _scrollToObjectId = id; }

		bool getBoneHierarchyPopupOpen() const { return _bBoneHierarchyPopupOpen; }
		void setBoneHierarchyPopupOpen( bool bOpen ) { _bBoneHierarchyPopupOpen = bOpen; }

		// ------------------------------------------------------------------------------
		// 7) 프리팹 애셋 매핑 (에디터 전용 메타데이터)
		// ------------------------------------------------------------------------------
		void		  setGameObjectPrefabPath( uint64 objectId, string_view prefabPath );
		const string& getGameObjectPrefabPath( uint64 objectId ) const;
		bool		  isGameObjectPrefabInstance( uint64 objectId ) const;

		// ------------------------------------------------------------------------------
		// 8) 뷰포트 카메라 북마크 (0~8 인덱스, 1~9 슬롯)
		// ------------------------------------------------------------------------------
		void				  setCameraBookmark( uint32 slot, const CameraBookmark& bookmark );
		const CameraBookmark* getCameraBookmark( uint32 slot ) const;
		bool				  hasCameraBookmark( uint32 slot ) const;
		void				  clearCameraBookmark( uint32 slot );

		// ------------------------------------------------------------------------------
		// 9) 컴포넌트 복사/붙여넣기 & 프리셋 (에디터 클립보드)
		// ------------------------------------------------------------------------------
		void		  copyComponent( const Component* pComp );
		bool		  hasCopiedComponent() const;
		const string& getCopiedComponentXml() const { return _copiedComponentXml; }
		const string& getCopiedComponentTypeName() const { return _copiedComponentTypeName; }
		bool		  pasteComponentValues( Component* pTargetComp );
		Component*	  pasteComponentAsNew( GameObject* pTargetObj );
		bool		  saveComponentPreset( const Component* pComp, string_view presetName );
		bool		  loadComponentPreset( Component* pComp, string_view presetFilePath );

		void alignSelectedObjects( AlignAxis axis, AlignType type );
		void distributeSelectedObjects( AlignAxis axis );
		void snapSelectedToGround();

		bool						isPrefabIsolationActive() const { return _bPrefabIsolation == SW_TRUE; }
		const string&				getPrefabIsolationPrefabPath() const;
		uint64						getPrefabIsolationRootId() const;
		const PrefabIsolationFrame* getPrefabIsolationFrame() const;
		void						pushPrefabIsolation( PrefabIsolationFrame frame );
		/** @brief 한 단계를 나갑니다. 스택이 비면 true입니다. */
		bool popPrefabIsolation();
		void clearPrefabIsolation();

		// ------------------------------------------------------------------------------
		// 10) 에디터 전용 GameObject UUID 매핑
		// ------------------------------------------------------------------------------
		Uuid		getOrAssignGuid( uint64 objectId );
		Uuid		getGuid( uint64 objectId ) const;
		void		setGuid( uint64 objectId, const Uuid& guid );
		uint64		findObjectIdByGuid( const Uuid& guid ) const;
		GameObject* findGameObjectByGuid( const Uuid& guid ) const;
		void		removeGuid( uint64 objectId );
		void		clearGuidMap();

	private:
		uint64						  _selectedComponentId;
		uint64						  _observedSceneGeneration;
		uint64						  _scrollToComponentId;
		uint64						  _scrollToObjectId;
		string						  _selectedComponentKey;
		string						  _focusedAssetPath;
		string						  _pendingOpenPanelTitle;
		string						  _pendingScenePath;
		string						  _pendingSceneActionPath;
		string						  _emptyString;
		string						  _copiedComponentXml;
		string						  _copiedComponentTypeName;
		mutex						  _pendingSceneMutex;
		array<CameraBookmark, 9>	  _arrCameraBookmark;
		vector<PrefabIsolationFrame>  _listPrefabIsolationFrame;
		unordered_map<uint64, string> _mapGameObjectToPrefab;
		unordered_map<uint64, Uuid>	  _mapObjectIdToGuid;
		unordered_map<Uuid, uint64>	  _mapGuidToObjectId;
		InspectMode					  _inspectMode;
		EditorPendingSceneAction	  _pendingSceneAction;
		int32						  _gizmoOperation;
		uint8						  _bGizmoLocalSpace		   : 1;
		uint8						  _bBoneHierarchyPopupOpen : 1;
		uint8						  _bSceneDirty			   : 1;
		uint8						  _bPrefabIsolation		   : 1;
		[[maybe_unused]] uint8		  _reservedWorkspace	   : 4;
	};
} // namespace sw::editor
