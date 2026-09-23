/**
 * @file EditorSceneCommands.h
 * @brief 하이어라키 · 뷰포트가 함께 쓰는 씬 오브젝트 변경 커맨드입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Workspace/SelectionManager.h"

namespace sw
{
    struct float3;

    class Component;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    struct EditorObjectSnapshot;

    /**
     * @class EditorSceneCommands
     * @brief GameObject 생성/복제/재부모/삭제/이름 변경을 트랜잭션과 함께 수행합니다.
     */
    class EditorSceneCommands
    {
    public:
        /** @brief 빈 GameObject를 만들고 SceneComponent를 붙인 뒤 선택합니다. */
        static GameObject* create( GameObjectManager* pManager, GameObject* pParent = nullptr );
        /** @brief XML 스냅샷으로 복제하고 같은 부모 아래에 붙입니다. */
        static GameObject* duplicate( GameObjectManager* pManager, GameObject* pSrc );
        /** @brief pChild 를 pNewParent 아래로 옮깁니다. 순환이 생기면 false 입니다. */
        static bool reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel = "Reparent GameObject" );
        /** @brief 부모에서 분리해 루트로 올립니다. */
        static bool unparent( GameObject* pObj, string_view undoLabel = "Unparent GameObject" );
        /** @brief Undo에 삭제를 기록하고 매니저에서 제거합니다. */
        static bool destroy( GameObjectManager* pManager, GameObject* pObj );
        /** @brief 이름을 바꾸고 Undo에 기록합니다. */
        static bool rename( GameObject* pObj, const utf8* pNewName );
        /** @brief 소유 오브젝트에서 컴포넌트를 제거합니다. */
        static bool destroyComponent( GameObjectManager* pManager, GameObject* pObj, Component* pComp );
        /** @brief 워크스페이스 선택을 바꿉니다. */
        static void select( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        /** @brief pNewParent가 pChild의 자손이면 true입니다. */
        static bool wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent );
        /** @brief 오브젝트 스냅샷(XML + 런타임 id)을 캡처합니다. nullptr 이면 빈 스냅샷입니다. */
        static EditorObjectSnapshot captureSnapshot( GameObject* pObj );
        /** @brief 로컬 트랜스폼을 적용합니다. */
        static void applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
                                         const float3& scale );
        /** @brief 아래 콜라이더/메시 윗면에 Y를 맞춥니다. */
        static void snapTranslationToSurface( GameObject* pObj, float3& translation, float32 scaleY );
        /** @brief @p before 와 지금 상태로 Undo를 기록합니다. */
        static void commitModify( GameObject* pObj, const EditorObjectSnapshot& before, string_view undoLabel );

        /** @brief 씬 통계의 한 줄입니다(컴포넌트 타입 이름과 그 인스턴스 수). */
        struct ComponentDistributionRow
        {
            string _typeName;
            uint32 _instanceCount{ 0 };
        };

        /** @brief 씬 전체 집계입니다. 행은 인스턴스 수 내림차순이고, 수가 같으면 이름 오름차순입니다. */
        struct SceneStatistics
        {
            uint32                           _objectCount{ 0 };
            uint32                           _rootCount{ 0 };
            uint32                           _componentCount{ 0 };
            vector<ComponentDistributionRow> _listDistribution;
        };

        /**
         * @brief 씬의 오브젝트 · 컴포넌트를 세고 타입별 분포를 만듭니다.
         * @details 예전에는 ProfilerPanel 이 타입 **이름 5개를 손으로 나열**하고 `getComponent<T>()` 로 각각 셌습니다. 그래서
         *          (1) 게임이 만든 컴포넌트는 표에 아예 나오지 않았고, (2) 한 오브젝트에 같은 타입이 여럿이어도 1 로 세서
         *          "Active Instances" 라는 열 이름과 맞지 않았습니다. 리플렉션 TypeInfo 로 묶으면 둘 다 해결되고, 엔진이
         *          컴포넌트를 늘려도 패널을 고칠 필요가 없습니다.
         */
        static SceneStatistics collectSceneStatistics( GameObjectManager* pManager );
    };
} // namespace sw::editor
