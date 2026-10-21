/**
 * @file EditorPrefabIsolation.h
 * @brief 프리팹 창(`PrefabEditorPanel`) 한 개의 격리 스택입니다. 창의 미리보기 씬 안에서 프리팹 하나만 보이게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    class EditorWorkspace;
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 격리에서 숨긴 오브젝트와 그 이전 활성 상태입니다. */
    struct PrefabIsolationHiddenObject
    {
        uint64 _objectID{ 0 };
        uint8  _bWasActive{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프리팹 격리의 한 단계입니다. */
    struct PrefabIsolationFrame
    {
        string                              _prefabPath;
        vector<PrefabIsolationHiddenObject> _listHidden;
        uint64                              _rootObjectID{ 0 };
        uint8                               _bSpawnedRoot{ SW_FALSE }; ///< 이 단계가 루트를 새로 스폰했다(원래 씬에 있던 중첩 부품이 아니다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPrefabIsolation
     * @brief 격리 단계를 쌓고 되돌립니다. 스택은 전역이 아니라 **창이 보관합니다**. 창의 씬이 사라지면 스택도 함께 사라지므로, 사라진 씬의 오브젝트 id 를 가진 채 남지 않습니다.
     * @details 맨 아래 단계는 창이 연 문서(프리팹)의 루트이고, 그 위 단계가 중첩 프리팹이다. 맨 아래 단계는 아무것도 숨기지 않는다. 그 위 단계는
     *          **문서 루트 아래**의 다른 오브젝트만 숨기고, 문서 루트 밖(조명, 바닥 같은 미리보기 환경)은 건드리지 않는다. 스폰에는 에셋 캐시와
     *          에디터 컨텍스트가 필요하므로 이 클래스는 스폰하지 않는다.
     */
    class SW_EDITOR_API EditorPrefabIsolation
    {
    public:
        bool                        isActive() const { return _listFrame.empty() == false; }
        size_t                      getDepth() const { return _listFrame.size(); }
        const PrefabIsolationFrame* getFrame() const { return _listFrame.empty() ? nullptr : &_listFrame.back(); }
        /** @brief 지금(맨 위) 단계의 프리팹 경로입니다. 단계가 없으면 빈 문자열입니다. */
        string_view getPrefabPath() const { return _listFrame.empty() ? string_view{} : string_view{ _listFrame.back()._prefabPath }; }
        /** @brief 지금 단계의 루트 오브젝트 id 입니다. 단계가 없으면 0 입니다. */
        uint64 getRootObjectID() const { return _listFrame.empty() ? 0 : _listFrame.back()._rootObjectID; }
        /** @brief 맨 아래 단계(문서 루트)의 오브젝트 id 입니다. 단계가 없으면 0 입니다. */
        uint64 getDocumentRootObjectID() const { return _listFrame.empty() ? 0 : _listFrame.front()._rootObjectID; }

        /**
         * @brief @p manager 의 씬에서 @p prefabPath 의 인스턴스를 찾습니다. @p pPreferred 가 맞으면 그것을 먼저 고릅니다.
         * @param pUnderRoot nullptr 이 아니면 이 루트 아래(자신 포함)만 봅니다.
         * @return 없으면 nullptr 이고, 부르는 쪽이 스폰합니다.
         */
        [[nodiscard]] static GameObject* findInstance( GameObjectManager& manager, const EditorWorkspace& workspace, string_view prefabPath, GameObject* pPreferred,
                                                       GameObject* pUnderRoot );

        /**
         * @brief @p root 를 격리하는 단계를 쌓고, 문서 루트 아래의 나머지 오브젝트를 숨깁니다.
         * @param bSpawnedRoot @p root 를 이 단계를 위해 새로 스폰했으면 true 입니다.
         */
        void push( GameObjectManager& manager, string_view prefabPath, GameObject& root, bool bSpawnedRoot );
        /**
         * @brief 맨 위 단계를 꺼내고 그 단계가 숨긴 오브젝트를 다시 보이게 합니다.
         * @param pManager 오브젝트를 다시 보이게 할 씬의 매니저입니다. nullptr 이면 단계만 꺼냅니다.
         * @param outFrame 꺼낸 단계입니다.
         * @return 단계가 있어 빠져나왔으면 true 입니다.
         */
        [[nodiscard]] bool pop( GameObjectManager* pManager, PrefabIsolationFrame& outFrame );
        /** @brief 모든 단계를 버립니다. 오브젝트는 다시 보이게 하지 않으며, 씬을 비우거나 문서를 바꿀 때 씁니다. */
        void clear() { _listFrame.clear(); }

    private:
        vector<PrefabIsolationFrame> _listFrame;
    };
} // namespace sw::editor
