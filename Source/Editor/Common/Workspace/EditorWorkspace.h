/**
 * @file EditorWorkspace.h
 * @brief 에디터 선택 / 애셋 포커스 / 기즈모 / 윈도우 열기 허브 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"

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
		int32  getGizmoOperation() const { return _gizmoOperation; }
		int32& getGizmoOperationRef() { return _gizmoOperation; }
		void   setGizmoOperation( int32 op ) { _gizmoOperation = op; }

		bool  isGizmoLocalSpace() const { return _bGizmoLocalSpace; }
		bool& getGizmoLocalSpaceRef() { return _bGizmoLocalSpace; }
		void  setGizmoLocalSpace( bool bLocal ) { _bGizmoLocalSpace = bLocal; }

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

		// ------------------------------------------------------------------------------
		// 6) 스크롤 타깃 · 본 계층 팝업
		// ------------------------------------------------------------------------------
		uint64 getScrollToComponentId() const { return _scrollToComponentId; }
		void   setScrollToComponentId( uint64 id ) { _scrollToComponentId = id; }

		uint64 getScrollToObjectId() const { return _scrollToObjectId; }
		void   setScrollToObjectId( uint64 id ) { _scrollToObjectId = id; }

		bool getBoneHierarchyPopupOpen() const { return _bBoneHierarchyPopupOpen; }
		void setBoneHierarchyPopupOpen( bool bOpen ) { _bBoneHierarchyPopupOpen = bOpen; }

	private:
		uint64		_selectedComponentId;
		string		_selectedComponentKey;
		string		_focusedAssetPath;
		InspectMode _inspectMode;
		int32		_gizmoOperation;
		string		_pendingOpenPanelTitle;
		string		_pendingScenePath;
		mutex		_pendingSceneMutex;
		uint64		_scrollToComponentId;
		uint64		_scrollToObjectId;
		bool		_bGizmoLocalSpace;
		bool		_bBoneHierarchyPopupOpen;
	};
} // namespace sw::editor
