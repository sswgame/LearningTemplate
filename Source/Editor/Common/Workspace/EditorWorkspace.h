/**
 * @file EditorWorkspace.h
 * @brief 에디터의 선택 · 애셋 포커스 · 기즈모 · 창 열기 요청을 모아 둔 곳입니다(EditorContext 소유).
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

#include "Editor/Common/Commands/EditorTransformCommands.h"
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
    /** @brief 에디터 뷰포트 카메라 북마크 (위치, 회전, 타깃) */
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
    /** @brief Isolation에서 숨긴 오브젝트와 이전 활성 상태 */
    struct PrefabIsolationHiddenObject
    {
        uint64 _objectId{ 0 };
        uint8  _bWasActive{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 제자리(in-place) 프리팹 Isolation 한 단계입니다. */
    struct PrefabIsolationFrame
    {
        string                              _prefabPath;
        vector<PrefabIsolationHiddenObject> _listHidden;
        uint64                              _rootObjectId{ 0 };
        uint8                               _bSpawnedRoot{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorWorkspace
     * @brief 에디터의 작업 공간 상태(선택, 포커스 애셋, 기즈모, 창 요청)를 관리합니다(EditorContext 소유).
     */
    class EditorWorkspace
    {
    public:
        explicit EditorWorkspace( EditorSelection* pEditorSelection );
        ~EditorWorkspace() = default;

        // ------------------------------------------------------------------------------
        // 1) 선택 — 오브젝트 / 컴포넌트
        // ------------------------------------------------------------------------------
        uint64 getSelectedObjectId() const;
        /** @brief 처음 선택한 오브젝트입니다. 선택이 없거나 사라졌으면 nullptr 입니다. 이번 호출 안에서만 쓰십시오. */
        GameObject* getSelectedObject() const;
        uint64      getSelectedComponentId() const { return _selectedComponentId; }
        void        setSelectedComponentId( uint64 id ) { _selectedComponentId = id; }
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
        // 3) 기즈모 — 조작 모드 / 로컬 스페이스
        // ------------------------------------------------------------------------------
        int32 getGizmoOperation() const { return _gizmoOperation; }
        void  setGizmoOperation( int32 op ) { _gizmoOperation = op; }

        bool isGizmoLocalSpace() const { return _bGizmoLocalSpace == SW_TRUE; }
        void setGizmoLocalSpace( bool bLocal ) { _bGizmoLocalSpace = ( bLocal ) ? SW_TRUE : SW_FALSE; }

        // ------------------------------------------------------------------------------
        // 4) 창 열기 요청 (메뉴가 넣고 셸이 소비한다)
        // ------------------------------------------------------------------------------
        void requestOpenPanel( const utf8* pTitle );
        bool consumeOpenPanel( string& outTitle );

        // ------------------------------------------------------------------------------
        // 5) 씬 열기 (파일 대화 상자는 백그라운드, 소비는 메인 스레드)
        // ------------------------------------------------------------------------------
        void requestLoadScene( string_view path );
        bool consumeLoadScene( string& outPath );

        /** @brief 미저장 확인 뒤에 이어서 할 씬 동작을 넣습니다. */
        void                     setPendingSceneAction( EditorPendingSceneAction action, string_view loadPath = {} );
        EditorPendingSceneAction getPendingSceneAction() const { return _pendingSceneAction; }
        const string&            getPendingSceneActionPath() const { return _pendingSceneActionPath; }
        void                     clearPendingSceneAction();

        /** @brief 마지막으로 동기화한 씬 세대입니다. */
        uint64 getObservedSceneGeneration() const { return _observedSceneGeneration; }
        void   setObservedSceneGeneration( uint64 generation ) { _observedSceneGeneration = generation; }

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

        // ------------------------------------------------------------------------------
        // 7) 프리팹 인스턴스 경로 — 활성 씬(`Scene::getEntityPrefabPath`)이 정본이고 여기는 창구일 뿐입니다
        // ------------------------------------------------------------------------------
        /** @brief 활성 씬에 오브젝트의 프리팹 경로를 적습니다. 빈 경로는 연결을 끊습니다. 활성 씬이 없으면 아무것도 하지 않습니다. */
        void setGameObjectPrefabPath( uint64 objectId, string_view prefabPath );
        /** @brief 활성 씬이 적어 둔 오브젝트의 프리팹 경로입니다. 프리팹 인스턴스가 아니거나 활성 씬이 없으면 빈 문자열입니다. */
        const string& getGameObjectPrefabPath( uint64 objectId ) const;

        // ------------------------------------------------------------------------------
        // 8) 뷰포트 카메라 북마크 (0~8 인덱스, 1~9 슬롯)
        // ------------------------------------------------------------------------------
        void                  setCameraBookmark( uint32 slot, const CameraBookmark& bookmark );
        const CameraBookmark* getCameraBookmark( uint32 slot ) const;
        bool                  hasCameraBookmark( uint32 slot ) const;
        void                  clearCameraBookmark( uint32 slot );

        // ------------------------------------------------------------------------------
        // 9) 컴포넌트 복사/붙여넣기 & 프리셋 (에디터 클립보드)
        // ------------------------------------------------------------------------------
        void               copyComponent( const Component* pComp );
        bool               hasCopiedComponent() const;
        const string&      getCopiedComponentTypeName() const { return _copiedComponentTypeName; }
        bool               pasteComponentValues( Component* pTargetComp );
        Component*         pasteComponentAsNew( GameObject* pTargetObj );
        [[nodiscard]] bool saveComponentPreset( const Component* pComp, string_view presetName );
        [[nodiscard]] bool loadComponentPreset( Component* pComp, string_view presetFilePath );

        // 정렬 · 분배 · 바닥 스냅은 여기 전달자를 두지 않는다. 커맨드 레지스트리(transform.*)를 거치고,
        // 로직은 EditorTransformCommands 에 있다.

        bool                        isPrefabIsolationActive() const { return _bPrefabIsolation == SW_TRUE; }
        const string&               getPrefabIsolationPrefabPath() const;
        uint64                      getPrefabIsolationRootId() const;
        const PrefabIsolationFrame* getPrefabIsolationFrame() const;
        void                        pushPrefabIsolation( PrefabIsolationFrame frame );
        /** @brief 한 단계를 나갑니다. 스택이 비면 true입니다. */
        bool popPrefabIsolation();
        void clearPrefabIsolation();

    private:
        EditorSelection*             _pEditorSelection;
        uint64                       _selectedComponentId;
        uint64                       _observedSceneGeneration;
        uint64                       _scrollToComponentId;
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
        int32                        _gizmoOperation;
        EditorPendingSceneAction     _pendingSceneAction;
        uint8                        _bGizmoLocalSpace  : 1;
        uint8                        _bSceneDirty       : 1;
        uint8                        _bPrefabIsolation  : 1;
        [[maybe_unused]] uint8       _reservedWorkspace : 4;
    };
} // namespace sw::editor
