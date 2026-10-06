/**
 * @file EditorSceneCommands.h
 * @brief 하이어라키 · 뷰포트가 함께 쓰는 씬 오브젝트 변경 커맨드입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/EditorSelection.h"

namespace sw
{
    struct float3;
    struct ObjectSnapshot;
    struct TagID;
    struct TypeInfo;

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
        /** @brief pChild 를 pNewParent 아래로 옮깁니다. 순환이 생기면 false 입니다. */
        static bool reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel = "Reparent GameObject" );
        /** @brief 부모에서 분리해 루트로 올립니다. */
        static bool unparent( GameObject* pObj, string_view undoLabel = "Unparent GameObject" );
        /** @brief Undo에 삭제를 기록하고 매니저에서 제거합니다. */
        static bool destroy( GameObjectManager* pManager, GameObject* pObj );
        /**
         * @brief 고른 오브젝트들을 되돌리기 한 단계로 지웁니다. 고른 조상이 있는 오브젝트는 그 조상과 함께 지워지므로 건너뜁니다. 지운 수를 돌려줍니다.
         */
        static uint32 destroyObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject );
        /** @brief 고른 오브젝트들을 되돌리기 한 단계로 복제하고 새 오브젝트를 @p outListCreated 에 담습니다(먼저 비운다). */
        static void duplicateObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject, vector<GameObject*>& outListCreated );
        /** @brief 이름을 바꾸고 Undo에 기록합니다. */
        [[nodiscard]] static bool rename( GameObject* pObj, const utf8* pNewName );
        /**
         * @brief 오브젝트 활성 비트를 바꿉니다. 멈춰 있으면 되돌리기와 씬 dirty 에 남기고, 플레이 중이면 바로 바꿉니다(Stop 이 되돌린다).
         * @details 계층 창의 눈 단추와 인스펙터의 Active 칸이 같이 쓴다. 같은 값이면 아무것도 하지 않는다.
         */
        static void setActive( GameObject* pObj, bool bActive );
        /**
         * @brief 타입 이름으로 컴포넌트를 붙이고 Undo에 기록합니다. 붙이지 못하면 nullptr 입니다.
         * @details 새 컴포넌트에 `onPostLoad` 를 부릅니다 — 기본값이 그 상태이고, 값을 자원으로 바꾸는 자리가 거기다(메시 · 머티리얼).
         *          컴포넌트를 만드는 편집기 길은 모두 이것(또는 `onPostLoad` 를 부르는 붙여넣기)을 지난다. 주의: 매니저에 바로 붙이면
         *          기록도 dirty 도 없어 되돌릴 수 없고 저장을 묻지 않고 사라지며, 붙인 메시는 플레이 전까지 그려지지 않는다.
         */
        static Component* addComponent( GameObject* pObj, const hashed_string& typeName );
        /** @brief 소유 오브젝트에서 컴포넌트를 제거합니다. */
        static bool destroyComponent( GameObjectManager* pManager, GameObject* pObj, Component* pComp );
        /** @brief 워크스페이스 선택을 바꿉니다. */
        static void select( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        /**
         * @brief @p pComponentType(또는 그 파생) 컴포넌트를 가진 오브젝트를 모읍니다(지울 표시가 된 것은 뺍니다). 순서는 매니저 순서입니다.
         * @details "같은 종류 모두 선택" 의 판정입니다(유니티 Select All of Type). 파생을 넣는 것은 `forEachComponentOfType` 과 같은 규칙입니다.
         */
        static void collectObjectsWithComponent( GameObjectManager& manager, const TypeInfo* pComponentType, vector<GameObject*>& outListObject );
        /** @brief 태그(@p tag 와 그 아래 계층 — `Enemy` 는 `Enemy.Boss` 도)를 단 오브젝트를 모읍니다. */
        static void collectObjectsWithTag( GameObjectManager& manager, TagID tag, vector<GameObject*>& outListObject );
        /** @brief 모은 오브젝트로 선택을 바꿉니다. 고른 수를 돌려줍니다(0 이면 선택을 건드리지 않습니다). */
        static uint32 selectObjects( const vector<GameObject*>& listObject );
        /** @brief pNewParent가 pChild의 자손이면 true입니다. */
        static bool wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent );
        /** @brief 로컬 트랜스폼을 적용합니다. */
        static void applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
                                         const float3& scale );
        /**
         * @brief 월드 트랜스폼을 적용합니다(`SceneComponent::setWorldTransform` — 부모 기준으로 분해하고 엔진의 오일러 규칙으로 적습니다).
         * @details 기즈모가 씁니다. 주의: ImGuizmo 의 XYZ 오일러로 분해한 값을 로컬 회전에 넣으면 엔진의 요 · 피치 · 롤 순서와 달라 두 축 이상이
         *          섞인 회전이 다른 회전으로 들어갑니다.
         */
        static void applyWorldTransform( GameObject* pObj, const float4x4& worldMatrix );
        /** @brief 아래 콜라이더/메시 윗면에 **월드** Y 를 맞춥니다(@p translation 은 월드 위치, @p scaleY 는 월드 Y 스케일). */
        static void snapTranslationToSurface( GameObject* pObj, float3& translation );
        /** @brief @p before 와 지금 상태로 Undo를 기록합니다. */
        static void commitModify( GameObject* pObj, const ObjectSnapshot& before, string_view undoLabel );

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
         * @details 리플렉션 TypeInfo 로 묶으므로 게임이 만든 컴포넌트도 표에 나오고, 한 오브젝트에 같은 타입이 여럿이면 그 수대로 셉니다
         *          ("Active Instances"). 엔진이 컴포넌트를 늘려도 패널을 고칠 필요가 없습니다.
         */
        static SceneStatistics collectSceneStatistics( GameObjectManager* pManager );
    };
} // namespace sw::editor
