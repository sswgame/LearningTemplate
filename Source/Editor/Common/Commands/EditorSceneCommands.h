/**
 * @file EditorSceneCommands.h
 * @brief 하이라키/뷰포트가 공유하는 씬 오브젝트 변이 커맨드
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
        /** @brief pChild를 pNewParent 아래로 옮깁니다. 사이클이면 false입니다. */
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
        /** @brief 오브젝트 XML 스냅샷을 캡처합니다. */
        static string captureSnapshot( GameObject* pObj );
        /** @brief 로컬 트랜스폼을 적용합니다. */
        static void applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
                                         const float3& scale );
        /** @brief 아래 콜라이더/메시 윗면에 Y를 맞춥니다. */
        static void snapTranslationToSurface( GameObject* pObj, float3& translation, float32 scaleY );
        /** @brief 전/후 스냅샷으로 Undo를 기록합니다. */
        static void commitModify( GameObject* pObj, string_view beforeXml, string_view undoLabel );
    };
} // namespace sw::editor
