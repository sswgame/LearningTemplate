/**
 * @file EditorSelection.h
 * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태 관리자 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    struct ObjectSnapshot;

    class CommandStack;
    class GameObject;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 선택 모드 */
    enum class SelectionMode : uint8
    {
        Replace = 0, ///< 기존 선택을 지우고 새로 선택
        Add,         ///< 기존 선택에 추가
        Remove,      ///< 기존 선택에서 제거
        Toggle       ///< 이미 선택되어 있으면 제거, 아니면 추가
    };

    /**
     * @class EditorSelection
     * @brief 에디터의 게임 오브젝트 · 애셋 다중 선택 상태를 관리합니다.
     * @details 선택은 프레임을 넘겨 드는 참조라 핸들(`GameObjectHandle`)로 보관하고, 쓸 때마다 자기 범위의 씬에서 풉니다(`findObject`).
     *          입력도 `GameObject*` 로 받고 반환하는 것도 푼 포인터입니다. 이번 호출 안에서만 쓰십시오. 되돌리기 · 플레이 세션 복원은
     *          같은 id 로 오브젝트를 복원하므로 그 너머로도 선택이 이어집니다.
     *
     *          **범위**: 에디터 컨텍스트가 가진 하나(메인 선택)는 편집 중인 씬(`editor::findGameObject`)과 전역 Undo 스택을 쓴다. 미리보기 창
     *          (`EditorPreviewPanel`)은 자기 인스턴스를 자기 씬 · 자기 스택에 묶는다(`bindPreviewScope`, `docs/plans/EditorMultiWorld.md` 2.3).
     *          인스펙터 · 기즈모처럼 "지금 겨누는 선택" 을 쓰는 곳은 `editor::getFocusedSelection` 하나로 묻는다 — 가장 최근에 바뀌거나 포커스를 얻은
     *          선택이다(메인 선택이 바뀌거나 메인 씬 창이 포커스를 얻으면 메인으로 돌아온다). 타입 조회로는 얻지 못한다(`kAccessorOnly`) —
     *          `EditorService.h` 의 세 접근자 중 하나를 골라 호출부에 어느 선택인지 적는다.
     */
    class SW_EDITOR_API EditorSelection : public EditorSubsystem
    {
    public:
        EditorSelection()                                    = default;
        EditorSelection( const EditorSelection& )            = delete;
        EditorSelection& operator=( const EditorSelection& ) = delete;
        /** @brief 미리보기 씬에 묶인 채 사라지면 묶음을 풉니다(`unbindPreviewScope`) — 목록(`editor::getPreviewSelections`)에 빈 포인터를 남기지 않는다. */
        ~EditorSelection() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorSelection";
        /** @brief 타입으로 찾지 못한다 — 메인 · 창마다 여럿이라 `editor::getMainSceneSelection` · `getFocusedSelection` · `findSelectionOf` 로 얻는다(`kIsAccessorOnlySubsystem`). */
        static constexpr bool kAccessorOnly = true;
        const utf8*           getSubsystemName() const override { return kSubsystemName; }

        // ------------------------------------------------------------------------------
        // 멤버 메서드
        // ------------------------------------------------------------------------------
        void selectObject( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        void selectObjects( const vector<GameObject*>& listObj, SelectionMode mode = SelectionMode::Replace );
        bool hasObject( const GameObject* pObj ) const;
        /** @brief 처음 선택한 오브젝트입니다. 선택이 없거나 사라졌으면 nullptr 입니다. */
        GameObject* getPrimaryObject() const;
        uint64      getPrimaryObjectID() const;
        /** @brief 선택한 순서의 핸들입니다. 사라진 대상이 섞여 있을 수 있으니, 풀어 쓰려면 `getSelectedObjects` 를 쓰십시오. */
        const vector<GameObjectHandle>& getSelectedHandles() const { return _listSelectedObject; }
        /** @brief 아직 살아 있는 선택 오브젝트를 선택한 순서로 @p outListObject 에 채웁니다(먼저 비웁니다). */
        void   getSelectedObjects( vector<GameObject*>& outListObject ) const;
        size_t getSelectedObjectCount() const { return _listSelectedObject.size(); }

        void                  selectAsset( string_view assetPath, SelectionMode mode = SelectionMode::Replace );
        void                  selectAssets( const vector<string>& listAssetPath, SelectionMode mode = SelectionMode::Replace );
        bool                  hasAsset( string_view assetPath ) const;
        string_view           getPrimaryAsset() const;
        const vector<string>& getSelectedAssets() const { return _listSelectedAsset; }

        void              clearObjectSelection();
        void              clearAssetSelection();
        void              clearAll();
        void              pruneInvalid();
        Delegate<void()>& onSelectionChanged() { return _onSelectionChanged; }

        // ------------------------------------------------------------------------------
        // 범위 — 메인 선택(묶지 않음) 또는 미리보기 창의 씬 · 스택
        // ------------------------------------------------------------------------------
        /**
         * @brief 이 선택을 미리보기 씬 @p scene 과 창의 Undo 스택 @p stack 에 묶습니다. 핸들은 그 씬에서 풀고, 편집 기록(`recordModify`)은 그 스택에 남습니다.
         * @details 창이 씬을 만든 직후 부르고, 창이 내려갈 때 `unbindPreviewScope` 로 풉니다. 묶인 동안 둘 다 이 선택보다 오래 살아야 합니다.
         */
        void bindPreviewScope( Scene& scene, CommandStack& stack );
        /** @brief 묶음을 풀고 선택을 비웁니다. 이 선택이 겨누는 선택이었으면 메인 선택으로 돌립니다(`editor::setFocusedSelection`). */
        void unbindPreviewScope();
        /** @brief 미리보기 씬에 묶인 선택이면 true 입니다. */
        bool isPreviewScope() const { return _pPreviewScene != nullptr; }
        /** @brief 묶인 창의 Undo 스택입니다. 메인 선택이면 nullptr 입니다(메인은 에디터 전역 스택 — `EditorTransaction`). */
        CommandStack* findPreviewCommandStack() const { return _pPreviewStack; }
        /** @brief 이 선택의 오브젝트가 사는 씬입니다 — 묶인 미리보기 씬, 아니면 편집 중인 씬입니다. 없으면 nullptr 입니다. */
        Scene* getScene() const;
        /** @brief 이 선택의 범위에서 핸들을 풉니다. 사라졌으면 nullptr 입니다. */
        GameObject* findObject( GameObjectHandle handle ) const;
        /**
         * @brief @p obj 의 수정 전후 상태를 이 선택의 범위에 기록합니다 — 미리보기 선택은 묶인 스택(그 씬의 키를 기억하는 명령), 메인 선택은
         *        에디터 트랜잭션(`EditorTransaction::recordModify`)입니다.
         */
        void recordModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label ) const;

    private:
        void notifyChanged();

    private:
        vector<GameObjectHandle> _listSelectedObject;
        vector<string>           _listSelectedAsset;
        Delegate<void()>         _onSelectionChanged;
        Scene*                   _pPreviewScene{ nullptr }; ///< 묶인 미리보기 씬(빌림). nullptr 이면 메인 선택이다
        CommandStack*            _pPreviewStack{ nullptr }; ///< 묶인 창의 Undo 스택(빌림)
    };
} // namespace sw::editor
