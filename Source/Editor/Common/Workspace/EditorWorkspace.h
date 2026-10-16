/**
 * @file EditorWorkspace.h
 * @brief 에디터의 선택, 애셋 포커스, 기즈모, 창 열기 요청을 모아 둡니다. `EditorContext` 가 소유합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 에디터 뷰포트 카메라 북마크입니다. 위치, 회전, 타깃을 저장합니다. */
    struct CameraBookmark
    {
        string  _name;
        float3  _position{ 0.0f, 0.0f, 0.0f };
        float3  _rotation{ 0.0f, 0.0f, 0.0f };
        float3  _orbitTarget{ 0.0f, 0.0f, 0.0f };
        float32 _orbitDistance{ 5.0f };
        bool    _bValid{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief Isolation 에서 숨긴 오브젝트와 그 이전 활성 상태입니다. */
    struct PrefabIsolationHiddenObject
    {
        uint64 _objectID{ 0 };
        uint8  _bWasActive{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 제자리(in-place) 프리팹 Isolation 의 한 단계입니다. */
    struct PrefabIsolationFrame
    {
        string                              _prefabPath;
        vector<PrefabIsolationHiddenObject> _listHidden;
        uint64                              _rootObjectID{ 0 };
        uint8                               _bSpawnedRoot{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorWorkspace
     * @brief 에디터의 작업 공간 상태(선택, 포커스 애셋, 기즈모, 창 요청)를 관리합니다. `EditorContext` 가 소유합니다.
     */
    class SW_EDITOR_API EditorWorkspace : public EditorSubsystem
    {
    public:
        explicit EditorWorkspace( EditorSelection* pEditorSelection );
        ~EditorWorkspace() override = default;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorWorkspace";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;

        // ------------------------------------------------------------------------------
        // 1) 선택: 오브젝트와 컴포넌트
        // ------------------------------------------------------------------------------
        uint64 getSelectedObjectID() const;
        /** @brief 처음 선택한 오브젝트입니다. 선택이 없거나 사라졌으면 nullptr 입니다. 이번 호출 안에서만 쓰세요. */
        GameObject* getSelectedObject() const;
        uint64      getSelectedComponentID() const { return _selectedComponentID; }
        void        setSelectedComponentID( uint64 id ) { _selectedComponentID = id; }
        string      getSelectedObjectName() const;
        void        setSelectedComponentKey( string_view key ) { _selectedComponentKey = key; }
        void        clearSelection();

        void selectGameObject( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        void selectComponent( GameObject* pObj, Component* pComp );
        void remapSelectionByObjectName( GameObjectManager* pGameObjectManager );

        // ------------------------------------------------------------------------------
        // 2) 애셋 포커스
        // ------------------------------------------------------------------------------
        const string& getFocusedAssetPath() const { return _focusedAssetPath; }
        void          setFocusedAssetPath( const utf8* pPath );

        // ------------------------------------------------------------------------------
        // 3) 기즈모: 조작 모드와 로컬 스페이스
        // ------------------------------------------------------------------------------
        int32 getGizmoOperation() const { return _gizmoOperation; }
        void  setGizmoOperation( int32 op ) { _gizmoOperation = op; }

        bool isGizmoLocalSpace() const { return _bGizmoLocalSpace == SW_TRUE; }
        /** @brief 뷰포트에서 선택하지 못하게 잠갔으면 true 입니다. Hierarchy 에서는 선택할 수 있습니다. 유니티의 Pickability, 언리얼의 잠금에 해당합니다. 저장되지 않습니다. */
        bool isObjectLocked( uint64 objectID ) const;
        /** @brief 뷰포트 선택 잠금을 걸거나 풉니다. */
        void setObjectLocked( uint64 objectID, bool bLocked );
        void setGizmoLocalSpace( bool bLocal ) { _bGizmoLocalSpace = ( bLocal ) ? SW_TRUE : SW_FALSE; }

        // ------------------------------------------------------------------------------
        // 4) 창 열기 요청. 메뉴가 넣고 셸이 소비한다
        // ------------------------------------------------------------------------------
        void requestOpenPanel( const utf8* pTitle );
        bool consumeOpenPanel( string& outTitle );

        // ------------------------------------------------------------------------------
        // 5) 씬 열기. 파일 대화상자는 백그라운드에서 열고, 소비는 메인 스레드에서 한다
        // ------------------------------------------------------------------------------
        void requestLoadScene( string_view path );
        bool consumeLoadScene( string& outPath );

        /** @brief 미저장 확인 뒤에 이어서 할 씬 동작을 등록합니다. */
        void                     setPendingSceneAction( EditorPendingSceneAction action, string_view loadPath = {} );
        EditorPendingSceneAction getPendingSceneAction() const { return _pendingSceneAction; }
        const string&            getPendingSceneActionPath() const { return _pendingSceneActionPath; }
        void                     clearPendingSceneAction();

        /** @brief 마지막으로 동기화한 씬 세대입니다. */
        uint64 getObservedSceneGeneration() const { return _observedSceneGeneration; }
        void   setObservedSceneGeneration( uint64 generation ) { _observedSceneGeneration = generation; }

        /** @brief 활성 씬에 저장되지 않은 에디터 변경이 있음을 표시합니다. */
        void markSceneDirty() { _bSceneDirty = SW_TRUE; }
        /** @brief 씬 dirty 플래그를 지웁니다. 저장이나 로드에 성공했을 때 호출합니다. */
        void clearSceneDirty() { _bSceneDirty = SW_FALSE; }
        /** @brief 저장하지 않은 씬 변경이 있으면 true 입니다. */
        bool isSceneDirty() const { return _bSceneDirty == SW_TRUE; }

        // ------------------------------------------------------------------------------
        // 6) 스크롤 타깃과 본 계층 팝업
        // ------------------------------------------------------------------------------
        uint64 getScrollToComponentID() const { return _scrollToComponentID; }
        void   setScrollToComponentID( uint64 id ) { _scrollToComponentID = id; }

        // ------------------------------------------------------------------------------
        // 7) 프리팹 인스턴스 경로. 인스턴스 기록은 둘이다 — 씬에 놓은 인스턴스(활성 씬의 `Scene::getEntityPrefabPath`)와,
        //    다른 프리팹 인스턴스가 스폰한 중첩 부품(그 씬의 `PrefabInstanceRegistry`). "이 오브젝트는 어느 프리팹의 인스턴스인가" 는
        //    `findPrefabPath` 하나로 묻는다. 놓은 인스턴스 기록만 따로 보는 일(복사 · 중첩으로 옮기기)만 `getPlacedPrefabPath` 를 쓴다.
        // ------------------------------------------------------------------------------
        /** @brief 활성 씬에 놓은 인스턴스의 프리팹 경로를 기록합니다. 빈 경로는 연결을 끊습니다. 활성 씬이 없으면 아무것도 하지 않습니다. */
        void setPlacedPrefabPath( uint64 objectID, string_view prefabPath );
        /** @brief 활성 씬에 놓은 인스턴스로 기록된 프리팹 경로입니다. 중첩 부품은 여기 없습니다(`findPrefabPath`). 없거나 활성 씬이 없으면 빈 문자열입니다. */
        string_view getPlacedPrefabPath( uint64 objectID ) const;
        /**
         * @brief @p pObject 가 인스턴스인 프리팹의 경로입니다 — 아직 바깥 인스턴스 아래에 붙은 중첩 부품이면 그 부품의 원본 프리팹, 아니면 놓은 인스턴스 기록입니다.
         * @details 인스펙터의 프리팹 줄 · 계층의 파란 이름 · Apply/Revert 대상 · 프리팹 격리가 모두 이 판정을 쓴다. 프리팹 인스턴스가 아니면 빈 문자열입니다.
         */
        string_view findPrefabPath( const GameObject* pObject ) const;
        /** @brief @p pObject 가 바깥 프리팹 인스턴스가 스폰한 중첩 부품(아직 그 아래에 붙음)이면 true 입니다. 부품은 연결을 따로 끊을 수 없습니다(바깥 인스턴스가 다시 스폰한다). */
        bool isNestedPrefabPart( const GameObject* pObject ) const;

        // ------------------------------------------------------------------------------
        // 8) 뷰포트 카메라 북마크. 인덱스는 0~8이고 슬롯은 1~9다
        // ------------------------------------------------------------------------------
        void                  setCameraBookmark( uint32 slot, const CameraBookmark& bookmark );
        const CameraBookmark* getCameraBookmark( uint32 slot ) const;
        bool                  hasCameraBookmark( uint32 slot ) const;
        void                  clearCameraBookmark( uint32 slot );

        // ------------------------------------------------------------------------------
        // 9) 컴포넌트 복사, 붙여 넣기, 프리셋(에디터 클립보드)
        // ------------------------------------------------------------------------------
        void          copyComponent( const Component* pComp );
        bool          hasCopiedComponent() const;
        const string& getCopiedComponentTypeName() const { return _copiedComponentTypeName; }
        /** @brief 복사한 컴포넌트의 바이너리 값입니다. 붙여 넣기는 `EditorTransformCommands::pasteCopiedComponentValues` 가 합니다. */
        const vector<uint8>& getCopiedComponentBytes() const { return _copiedComponentBytes; }
        /** @brief 바이너리로 옮기지 못하는 값의 XML 대체 사본입니다. */
        const string& getCopiedComponentXML() const { return _copiedComponentXML; }

        // 붙여 넣기, 프리셋, 정렬, 분배, 바닥 스냅은 여기에 전달 함수를 두지 않는다. 로직은 EditorTransformCommands 에 있고(작업 공간은 클립보드만 보관한다),
        // 커맨드 레지스트리(transform.*)를 거친다.

        bool                        isPrefabIsolationActive() const { return _bPrefabIsolation == SW_TRUE; }
        const string&               getPrefabIsolationPrefabPath() const;
        uint64                      getPrefabIsolationRootID() const;
        const PrefabIsolationFrame* getPrefabIsolationFrame() const;
        void                        pushPrefabIsolation( PrefabIsolationFrame frame );
        /** @brief 한 단계를 나갑니다. 스택이 비면 true 입니다. */
        bool popPrefabIsolation();
        void clearPrefabIsolation();

    private:
        EditorSelection*             _pEditorSelection;
        uint64                       _selectedComponentID;
        uint64                       _observedSceneGeneration;
        uint64                       _scrollToComponentID;
        string                       _selectedComponentKey;
        string                       _focusedAssetPath;
        string                       _pendingOpenPanelTitle;
        string                       _pendingScenePath;
        string                       _pendingSceneActionPath;
        string                       _emptyString;
        string                       _copiedComponentXML;
        string                       _copiedComponentTypeName;
        vector<uint8>                _copiedComponentBytes;
        mutex                        _pendingSceneMutex;
        array<CameraBookmark, 9>     _arrCameraBookmark;
        vector<PrefabIsolationFrame> _listPrefabIsolationFrame;
        vector<uint64>               _listLockedObjectID; ///< 뷰포트 선택을 잠근 오브젝트 id
        int32                        _gizmoOperation;
        EditorPendingSceneAction     _pendingSceneAction;
        uint8                        _bGizmoLocalSpace  : 1;
        uint8                        _bSceneDirty       : 1;
        uint8                        _bPrefabIsolation  : 1;
        [[maybe_unused]] uint8       _reservedWorkspace : 4;
    };
} // namespace sw::editor
