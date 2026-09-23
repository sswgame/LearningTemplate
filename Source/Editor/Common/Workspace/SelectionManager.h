/**
 * @file SelectionManager.h
 * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태 관리자 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    class GameObject;
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
     * @class SelectionManager
     * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태를 관리하는 멤버 클래스
     * @details 선택은 프레임을 넘겨 드는 참조라 핸들(`GameObjectHandle`)로 보관하고, 쓸 때마다 편집 중인 씬에서 풉니다
     *          (`editor::findGameObject`). 받는 쪽은 `GameObject*` 로 받고 돌려주는 쪽도 푼 포인터입니다 — 이번 호출 안에서만
     *          쓰십시오. 되돌리기 · 플레이 세션 복원은 같은 id 로 오브젝트를 되살리므로 그 너머로도 선택이 이어집니다.
     */
    class SelectionManager
    {
    public:
        SelectionManager()  = default;
        ~SelectionManager() = default;

        // ------------------------------------------------------------------------------
        // 멤버 메서드
        // ------------------------------------------------------------------------------
        void selectObject( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        void selectObjects( const vector<GameObject*>& listObj, SelectionMode mode = SelectionMode::Replace );
        bool hasObject( const GameObject* pObj ) const;
        /** @brief 처음 선택한 오브젝트. 선택이 없거나 사라졌으면 nullptr. */
        GameObject* getPrimaryObject() const;
        uint64      getPrimaryObjectId() const;
        /** @brief 선택한 순서의 핸들. 사라진 대상이 섞여 있을 수 있습니다 — 풀어 쓰려면 `getSelectedObjects`. */
        const vector<GameObjectHandle>& getSelectedHandles() const { return _listSelectedObject; }
        /** @brief 아직 살아 있는 선택 오브젝트를 선택한 순서로 @p outListObject 에 채웁니다(비우고 채운다). */
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

    private:
        void notifyChanged();

    private:
        vector<GameObjectHandle> _listSelectedObject;
        vector<string>           _listSelectedAsset;
        Delegate<void()>         _onSelectionChanged;
    };
} // namespace sw::editor
