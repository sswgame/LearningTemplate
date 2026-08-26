/**
 * @file EditorWorkspace.h
 * @brief 에디터 선택 / 애셋 포커스 / 기즈모 / 윈도우 열기 허브 (Static Class)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
	class Component;
}

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
	 * @brief 에디터의 전역 작업 공간 상태(선택, 포커스 애셋, 기즈모, 윈도우 요청)를 총괄하는 정적 클래스
	 */
	class EditorWorkspace
	{
	public:
		EditorWorkspace() = delete;

		// ------------------------------------------------------------------------------
		// 1) 선택 — 오브젝트 / 컴포넌트
		// ------------------------------------------------------------------------------
		/** @brief 현재 주 선택된 게임 오브젝트의 ID를 반환합니다. */
		static uint64 selectedObjectId();
		/** @brief 현재 주 선택된 게임 오브젝트 포인터를 반환합니다. */
		static GameObjectPtr selectedObject();
		/** @brief 현재 선택된 컴포넌트의 ID를 반환합니다. */
		static uint64& selectedComponentId();
		/** @brief 현재 선택된 게임 오브젝트의 이름을 반환합니다. */
		static string selectedObjectName();
		/** @brief Play->Stop 시 컴포넌트를 다시 찾기 위한 안정적인 키입니다. */
		static string& selectedComponentKey();
		/** @brief 현재 선택 상태를 초기화합니다. */
		static void clearSelection();

		/** @brief 특정 게임 오브젝트를 선택합니다. */
		static void selectGameObject( GameObjectPtr pObj, SelectionMode mode = SelectionMode::Replace );
		/** @brief 특정 게임 오브젝트의 컴포넌트를 선택합니다. */
		static void selectComponent( GameObjectPtr pObj, ComponentPtr pComp );
		/** @brief 이름 기반으로 선택된 오브젝트를 다시 매핑합니다. */
		static void remapSelectionByObjectName( GameObjectManager* pGameObjectManager );

		// ------------------------------------------------------------------------------
		// 2) 애셋 포커스 · 인스펙트 모드
		// ------------------------------------------------------------------------------
		/** @brief 현재 포커스된 애셋 경로를 반환합니다. */
		static string& focusedAssetPath();
		/** @brief 현재 포커스된 애셋 경로를 설정합니다. */
		static void setFocusedAssetPath( const utf8* pPath );

		/** @brief 현재 인스펙터 모드를 반환합니다. */
		static InspectMode& inspectMode();
		/** @brief 인스펙터 모드를 설정합니다. */
		static void setInspectMode( InspectMode mode );

		// ------------------------------------------------------------------------------
		// 3) 기즈모 — 조작 모드 / 로컬 스페이스
		// ------------------------------------------------------------------------------
		/** @brief 기즈모 조작 모드 (0=Translate, 1=Rotate, 2=Scale) */
		static int32& gizmoOperation();
		/** @brief 기즈모의 로컬 스페이스 사용 여부를 반환합니다. */
		static bool& gizmoLocalSpace();

		// ------------------------------------------------------------------------------
		// 4) 윈도우 열기 요청 — 메뉴가 쓰고 셸이 consume
		// ------------------------------------------------------------------------------
		/** @brief 열기를 요청한 윈도우의 타이틀을 반환합니다. */
		static string& pendingOpenPanelTitle();
		/** @brief 특정 윈도우의 열기를 요청합니다. */
		static void requestOpenPanel( const utf8* pTitle );
		/** @brief 요청된 윈도우 열기 이벤트를 소비합니다. */
		static bool consumeOpenPanel( string& outTitle );

		// ------------------------------------------------------------------------------
		// 5) 씬 열기 — FileDialog는 백그라운드, consume은 메인 스레드
		// ------------------------------------------------------------------------------
		/** @brief 파일 대화상자가 고른 씬 경로를 큐에 넣습니다. */
		static void requestLoadScene( string_view path );
		/** @brief 큐에 있는 씬 경로를 꺼냅니다. */
		static bool consumeLoadScene( string& outPath );

		// ------------------------------------------------------------------------------
		// 6) 스크롤 타깃 · 본 계층 팝업
		// ------------------------------------------------------------------------------
		/** @brief 스크롤 이동할 컴포넌트 ID를 반환합니다. */
		static uint64& scrollToComponentId();
		/** @brief 스크롤 이동할 오브젝트 ID를 반환합니다. */
		static uint64& scrollToObjectId();

		/** @brief 본 계층 구조 팝업의 열림 상태를 반환합니다. */
		static bool& boneHierarchyPopupOpen();
	};
} // namespace sw::editor
