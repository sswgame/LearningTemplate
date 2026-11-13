/**
 * @file EditorWorkspace.h
 * @brief 에디터의 선택, 애셋 포커스, 기즈모, 창 열기 요청을 모아 둡니다. `EditorContext` 가 소유합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 에디터 뷰포트 카메라 북마크입니다. 위치, 회전, 궤도 거리를 저장합니다. 궤도 중심은 이 셋에서 구하므로 따로 저장하지 않습니다. */
    struct CameraBookmark
    {
        string  _name;
        float3  _position{ 0.0f, 0.0f, 0.0f };
        float3  _rotation{ 0.0f, 0.0f, 0.0f };
        float32 _orbitDistance{ 5.0f };
        bool    _bValid{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패널 열기 요청 하나입니다. 패널 id 와 문서 종류 중 하나만 채웁니다(`EditorWorkspace::requestOpenPanel<T>` · `requestOpenDocumentPanel`). */
    struct EditorPanelOpenRequest
    {
        string          _panelID;            ///< 등록 id(`T::kPanelID`) 또는 실행 중에 들어온 id
        EditorAssetType _documentKind{};     ///< 이 종류의 문서를 여는 도구 패널. 비면 id 로 연다
        bool            _bFloat{ SW_FALSE }; ///< 도킹을 풀고 메인 창 밖으로 옮긴다 — ImGui 가 그 패널을 제 OS 창(보조 뷰포트)으로 띄운다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorWorkspace
     * @brief 에디터의 작업 공간 상태(선택, 포커스 애셋, 기즈모, 창 요청)를 관리합니다. `EditorContext` 가 소유합니다.
     */
    /**
     * @struct EditorAssetMoveEvent
     * @brief 에셋 옮김 알림 한 번의 내용입니다(`EditorWorkspace::notifyAssetsMoved`). `_listOldID[i]` 가 `_listNewID[i]` 로 갔다(같은 길이).
     * @details 글을 고친 참조 파일 목록이 함께 온다 — 깨끗하게 열린 참조 문서는 디스크의 고친 글을 다시 읽는다(감사 P0-4). 옛 id 를 든 채 두면 다음 저장이 고친 참조를 옛 id 로 덮는다.
     *          언리얼은 이름을 바꿀 때 로드된 참조자를 메모리에서 고치지만, 이 엔진은 문서마다 메모리 꼴이 달라 다시 읽기가 그 자리다.
     */
    struct EditorAssetMoveEvent
    {
        vector<string> _listOldID;       ///< 옮기기마다 옛 리소스 id
        vector<string> _listNewID;       ///< 같은 칸의 새 리소스 id
        vector<string> _listRewrittenID; ///< 글을 고친 참조 파일(옮긴 뒤의 id — 함께 옮긴 참조자는 새 id)
    };
    /** @brief 에셋 옮김 알림입니다(`EditorWorkspace::notifyAssetsMoved`). */
    using EditorAssetsMovedDelegate = Delegate<void( const EditorAssetMoveEvent& )>;

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
        /** @brief 여러 오브젝트를 한 번에 고릅니다(`EditorSelection::selectObjects` — 알림 한 번). 행마다 `selectGameObject` 를 부르면 제곱이다. */
        void selectGameObjects( const vector<GameObject*>& listObj, SelectionMode mode = SelectionMode::Replace );
        void selectComponent( GameObject* pObj, Component* pComp );
        void remapSelectionByObjectName( GameObjectManager* pGameObjectManager );

        // ------------------------------------------------------------------------------
        // 2) 애셋 포커스
        // ------------------------------------------------------------------------------
        const string& getFocusedAssetPath() const { return _focusedAssetPath; }
        void          setFocusedAssetPath( const utf8* pPath );
        /**
         * @brief 에셋을 옮겼다고 알립니다(`EditorAssetFileCommands::applyAssetMoves` 가 성공 때 한 번).
         * @details 포커스 경로 · 메인 선택과 미리보기 선택의 에셋 · 활성 씬의 소스 경로를 고치고, 구독한 쪽(열린 문서 패널 — 경로를 고치고 깨끗한 참조 문서는 다시 읽는다)에 알린다(감사 P0-4).
         *          예전에는 디스크 파일과 참조 글만 고쳐 열린 문서 · 씬이 옛 id 로 남았다 — 다음 저장이 옛 자리에 파일을 다시 만들고 옮긴 파일은 편집을 받지 못했다.
         */
        void notifyAssetsMoved( const EditorAssetMoveEvent& notice );
        /** @brief 에셋 옮김 알림입니다. */
        MulticastDelegate<void( const EditorAssetMoveEvent& )>& onAssetsMoved() { return _assetsMoved; }

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
        // 4) 창 열기 요청. 메뉴가 넣고 셸이 소비한다(`EditorMenuBar::processOpenPanelRequests`). 요청은 창 제목이 아니라 패널을 가리킨다 —
        //    제목을 키로 받으면 제목이 바뀐 패널의 요청이 말없이 버려진다. 제목은 셸이 찾은 인스턴스에서 읽고, 찾지 못하면 오류로 남긴다
        // ------------------------------------------------------------------------------
        /** @brief 패널 클래스로 열기를 요청합니다. id 는 그 클래스의 `kPanelID` 하나에서 온다(등록 · `findPanel<T>()` 와 같은 상수). */
        template <typename TPanel>
        void requestOpenPanel()
        {
            requestOpenPanelByID( TPanel::kPanelID );
        }
        /** @brief 실행 중에 들어온 id(콘솔 인자 · 패널 서비스)로 열기를 요청합니다. 코드가 아는 패널은 `requestOpenPanel<T>()` 를 씁니다. */
        void requestOpenPanelByID( string_view panelID );
        /** @brief 열린 패널의 도킹을 풀어 메인 창 밖의 제 OS 창으로 띄우기를 요청합니다(개발 명령 `panel.float` — 보조 창 그리기 · 스왑체인 경로를 시나리오가 본다). */
        void requestFloatPanelByID( string_view panelID );
        /** @brief 애셋 종류 @p kind 의 문서를 여는 도구 패널(`IEditorPanel::getDocumentKind`)을 열기를 요청합니다. */
        void requestOpenDocumentPanel( const EditorAssetType& kind );
        /** @brief 쌓인 요청을 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool consumeOpenPanel( EditorPanelOpenRequest& outRequest );

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

        /**
         * @brief 활성 씬에 저장되지 않은 에디터 변경이 있음을 표시합니다.
         * @details 씬을 바꾼 모든 편집이 지나는 한 곳이다(기록 · 되돌리기 · 개발 명령). 편집을 막는 작업(`EditorPlaySession::holdSceneEdits`) 중에 오면
         *          편집 허용 답을 묻지 않은 경로가 씬을 바꾼 것이다 — 오류를 남기고 그 작업이 알게 한다(`EditorPlaySession::recordSceneEditWhileHeld`,
         *          베이크는 결과를 버리고 멈춘다). 막는 작업 자신은 잠금을 푼 뒤에 표시한다.
         */
        void markSceneDirty();
        /** @brief 씬 dirty 플래그를 지웁니다. 저장이나 로드에 성공했을 때 호출합니다. */
        void clearSceneDirty() { _bSceneDirty = SW_FALSE; }
        /** @brief 저장하지 않은 씬 변경이 있으면 true 입니다. */
        bool isSceneDirty() const { return _bSceneDirty == SW_TRUE; }
        /** @brief 씬을 바꾼 편집(`markSceneDirty`)마다 하나씩 오르는 번호입니다. 자동 저장이 지난 사본 뒤 편집이 있었는지 본다. */
        uint32 getSceneEditSerial() const { return _sceneEditSerial; }

        // ------------------------------------------------------------------------------
        // 6) 스크롤 타깃과 본 계층 팝업
        // ------------------------------------------------------------------------------
        uint64 getScrollToComponentID() const { return _scrollToComponentID; }
        void   setScrollToComponentID( uint64 id ) { _scrollToComponentID = id; }

        // ------------------------------------------------------------------------------
        // 7) 프리팹 인스턴스 경로. 인스턴스 기록은 둘이다 — 씬에 놓은 인스턴스(그 오브젝트가 사는 씬의 `Scene::getEntityPrefabPath`)와,
        //    다른 프리팹 인스턴스가 스폰한 중첩 부품(그 씬의 `PrefabInstanceRegistry`). "이 오브젝트는 어느 프리팹의 인스턴스인가" 는
        //    `findPrefabPath` 하나로 묻는다. 놓은 인스턴스 기록만 따로 보는 일(복사 · 중첩으로 옮기기)만 `getPlacedPrefabPath` 를 쓴다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 놓은 인스턴스의 프리팹 경로를 **오브젝트가 사는 씬**(활성 씬이나 미리보기 씬)에 기록합니다. 빈 경로는 연결을 끊습니다. 어느 씬에도 없으면 아무것도 하지 않습니다.
         * @details 오브젝트 id 만 받으면 활성 씬으로 읽고 써서 미리보기 창(프리팹 창)의 인스턴스를 놓친다 — 그래서 id 가 아니라 오브젝트를 받는다.
         */
        void setPlacedPrefabPath( const GameObject& object, string_view prefabPath );
        /** @brief 놓은 인스턴스로 기록된 프리팹 경로입니다(오브젝트가 사는 씬의 테이블). 중첩 부품은 여기 없습니다(`findPrefabPath`). 없으면 빈 문자열입니다. */
        string_view getPlacedPrefabPath( const GameObject& object ) const;
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

        // ------------------------------------------------------------------------------
        // 9) 컴포넌트 복사, 붙여 넣기, 프리셋(에디터 클립보드)
        // ------------------------------------------------------------------------------
        void          copyComponent( const Component* pComp );
        bool          hasCopiedComponent() const;
        const string& getCopiedComponentTypeName() const { return _copiedComponentTypeName; }
        /** @brief 복사한 컴포넌트의 값입니다(XML — 프리셋 파일과 같은 형식 하나). 붙여 넣기는 `EditorTransformCommands::pasteCopiedComponentValues` 가 합니다. */
        const string& getCopiedComponentXML() const { return _copiedComponentXML; }

        // 붙여 넣기, 프리셋, 정렬, 분배, 바닥 스냅은 여기에 전달 함수를 두지 않는다. 로직은 EditorTransformCommands 에 있고(작업 공간은 클립보드만 보관한다),
        // 커맨드 레지스트리(transform.*)를 거친다.

    private:
        EditorSelection*                                       _pEditorSelection;
        uint64                                                 _selectedComponentID;
        uint64                                                 _observedSceneGeneration;
        uint64                                                 _scrollToComponentID;
        string                                                 _selectedComponentKey;
        string                                                 _focusedAssetPath;
        MulticastDelegate<void( const EditorAssetMoveEvent& )> _assetsMoved; ///< `notifyAssetsMoved`
        EditorPanelOpenRequest                                 _pendingOpenPanel;
        string _pendingScenePath                               SW_GUARDED_BY( _pendingSceneMutex );
        string                                                 _pendingSceneActionPath;
        string                                                 _copiedComponentXML;
        string                                                 _copiedComponentTypeName;
        mutex                                                  _pendingSceneMutex;
        array<CameraBookmark, 9>                               _arrCameraBookmark;
        vector<uint64>                                         _listLockedObjectID; ///< 뷰포트 선택을 잠근 오브젝트 id
        int32                                                  _gizmoOperation;
        EditorPendingSceneAction                               _pendingSceneAction;
        uint32                                                 _sceneEditSerial; ///< `markSceneDirty` 횟수(`getSceneEditSerial`)
        uint8                                                  _bGizmoLocalSpace  : 1;
        uint8                                                  _bSceneDirty       : 1;
        [[maybe_unused]] uint8                                 _reservedWorkspace : 5;
    };
} // namespace sw::editor
