/**
 * @file EditorSceneCommands.h
 * @brief 하이어라키와 뷰포트가 함께 쓰는 씬 오브젝트 변경 커맨드를 제공합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
     * @brief GameObject 의 생성, 복제, 부모 변경, 삭제, 이름 변경을 트랜잭션과 함께 수행합니다.
     */
    class EditorSceneCommands
    {
    public:
        /** @brief 빈 GameObject 를 만들고 SceneComponent 를 붙인 뒤 선택합니다. */
        static GameObject* create( GameObjectManager* pManager, GameObject* pParent = nullptr );
        /** @brief XML 스냅샷으로 복제해 같은 부모 아래에 붙입니다. */
        static GameObject* duplicate( GameObjectManager* pManager, GameObject* pSrc );
        /** @brief pChild 를 pNewParent 아래로 옮깁니다. 순환이 생기면 false 입니다. */
        static bool reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel = "Reparent GameObject" );
        /** @brief 부모에서 분리해 루트로 만듭니다. */
        static bool unparent( GameObject* pObj, string_view undoLabel = "Unparent GameObject" );
        /** @brief 삭제를 Undo 에 기록하고 매니저에서 제거합니다. */
        static bool destroy( GameObjectManager* pManager, GameObject* pObj );
        /**
         * @brief 선택한 오브젝트들을 되돌리기 한 단계로 지웁니다. 선택한 조상이 있는 오브젝트는 그 조상과 함께 지워지므로 건너뜁니다. 지운 수를 반환합니다.
         */
        static uint32 destroyObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject );
        /**
         * @brief 계층 창이 그릴 루트 오브젝트를 표시 순서(형제 번호, 같으면 오브젝트 id 오름차순)로 모읍니다. 먼저 출력 목록을 비우며, 지우는 중인 오브젝트는 뺍니다.
         * @details 스토어의 순서는 삭제에 따라 바뀌므로(swap-remove) 쓰지 않는다. 씬은 파일 순서로 id 를 매기므로 번호가 모두 0 인 씬은 처음 연 순서와 같고,
         *          되돌린 오브젝트는 원래 id 와 번호로 돌아와 제 위치에 선다. 번호를 고치는 동작은 `EditorSiblingOrder` 에 있다.
         */
        static void collectRootsInOrder( GameObjectManager& manager, vector<GameObject*>& outListRoot );
        /** @brief 선택한 오브젝트들을 되돌리기 한 단계로 복제하고 새 오브젝트를 @p outListCreated 에 담습니다. 먼저 출력 목록을 비웁니다. */
        static void duplicateObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject, vector<GameObject*>& outListCreated );
        /**
         * @brief 오브젝트를 에디터 클립보드에 서브트리째 복사합니다. 선택한 것 안의 자손은 한 번만 복사하며, 복사한 루트 수를 반환합니다.
         * @details 클립보드는 상태 바이트이므로 씬을 바꿔도 남는다. 다른 씬에 붙여 넣을 수 있다(유니티와 언리얼의 Ctrl+C/V).
         *          버퍼는 Engine 의 `ObjectCopyBuffer` 라 에디터 모듈을 핫 리로드해도 비워지지 않는다.
         */
        static uint32 copyObjects( const vector<GameObject*>& listObject );
        /** @brief 클립보드의 오브젝트를 @p pManager 의 루트로 붙여 넣습니다. 되돌리기 항목은 하나이며, 붙여 넣은 루트를 선택합니다. */
        static void pasteObjects( GameObjectManager* pManager, vector<GameObject*>& outListCreated );
        /** @brief 붙여 넣을 오브젝트가 있으면 true 입니다. */
        static bool hasCopiedObjects();
        /** @brief 이름을 바꾸고 Undo 에 기록합니다. */
        [[nodiscard]] static bool rename( GameObject* pObj, const utf8* pNewName );
        /**
         * @brief 오브젝트의 활성 비트를 바꿉니다. 정지 상태이면 되돌리기와 씬 dirty 에 기록하고, 플레이 중이면 바로 바꿉니다(Stop 이 되돌린다).
         * @details 계층 창의 눈 아이콘 버튼과 인스펙터의 Active 항목이 함께 쓴다. 같은 값이면 아무것도 하지 않는다.
         */
        static void setActive( GameObject* pObj, bool bActive );
        /**
         * @brief 타입 이름으로 컴포넌트를 붙이고 Undo 에 기록합니다. 붙이지 못하면 nullptr 입니다.
         * @details 새 컴포넌트에 `onPostLoad` 를 호출한다. 기본값이 그 상태이고, 값을 리소스로 바꾸는 곳이 거기다(메시, 머티리얼).
         *          에디터에서 컴포넌트를 만드는 경로는 모두 이 함수(또는 `onPostLoad` 를 호출하는 붙여넣기)를 지난다. 매니저에 바로 붙이면
         *          기록도 dirty 도 남지 않아 되돌릴 수 없고 저장을 묻지 않고 사라지며, 붙인 메시는 플레이 전까지 그려지지 않는다.
         */
        static Component* addComponent( GameObject* pObj, const hashed_string& typeName );
        /**
         * @brief 오브젝트를 에디터에서만 숨기거나 보입니다. 자식 오브젝트도 함께 처리합니다.
         * @details 저장, 되돌리기, 씬 dirty 에는 남지 않는다. 언리얼과 유니티의 눈 아이콘에 해당한다.
         *          그 오브젝트들의 메시 렌더 상태를 dirty 로 표시해 다음 프레임 수집에 반영한다.
         */
        static void setHiddenInEditor( GameObject* pObj, bool bHidden );
        /** @brief 소유 오브젝트에서 컴포넌트를 제거합니다. */
        static bool destroyComponent( GameObjectManager* pManager, GameObject* pObj, Component* pComp );
        /** @brief 워크스페이스 선택을 바꿉니다. */
        static void select( GameObject* pObj, SelectionMode mode = SelectionMode::Replace );
        /**
         * @brief @p pComponentType(또는 그 파생) 컴포넌트를 가진 오브젝트를 모읍니다. 삭제 표시가 된 오브젝트는 뺍니다. 순서는 매니저의 순서입니다.
         * @details "같은 종류 모두 선택" 의 판정이다(유니티의 Select All of Type). 파생 타입을 포함하는 규칙은 `forEachComponentOfType` 과 같다.
         */
        static void collectObjectsWithComponent( GameObjectManager& manager, const TypeInfo* pComponentType, vector<GameObject*>& outListObject );
        /** @brief 태그(@p tag 와 그 하위 계층)를 가진 오브젝트를 모읍니다. 예를 들어 `Enemy` 는 `Enemy.Boss` 도 포함합니다. */
        static void collectObjectsWithTag( GameObjectManager& manager, TagID tag, vector<GameObject*>& outListObject );
        /** @brief 모은 오브젝트로 선택을 바꿉니다. 선택한 수를 반환하며, 0 이면 선택을 건드리지 않습니다. */
        static uint32 selectObjects( const vector<GameObject*>& listObject );
        /** @brief pNewParent 가 pChild 의 자손이면 true 입니다. */
        static bool wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent );
        /** @brief 로컬 트랜스폼을 적용합니다. */
        static void applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
                                         const float3& scale );
        /**
         * @brief 월드 트랜스폼을 적용합니다. `SceneComponent::setWorldTransform` 이 부모 기준으로 분해하고 엔진의 오일러 규칙으로 기록합니다.
         * @details 기즈모가 쓴다. ImGuizmo 의 XYZ 오일러로 분해한 값을 로컬 회전에 넣으면 엔진의 요, 피치, 롤 순서와 달라서
         *          두 축 이상이 섞인 회전이 다른 회전으로 들어간다.
         */
        static void applyWorldTransform( GameObject* pObj, const float4x4& worldMatrix );
        /** @brief 아래에 있는 콜라이더나 메시의 윗면에 월드 Y 를 맞춥니다. @p translation 은 월드 위치, @p scaleY 는 월드 Y 스케일입니다. */
        static void snapTranslationToSurface( GameObject* pObj, float3& translation );
        /** @brief @p before 와 현재 상태로 Undo 를 기록합니다. */
        static void commitModify( GameObject* pObj, const ObjectSnapshot& before, string_view undoLabel );

        /** @brief 씬 통계의 한 행입니다. 컴포넌트 타입 이름과 그 인스턴스 수를 담습니다. */
        struct ComponentDistributionRow
        {
            string _typeName;
            uint32 _instanceCount{ 0 };
        };

        /** @brief 씬 전체 집계입니다. 행은 인스턴스 수의 내림차순이고, 수가 같으면 이름의 오름차순입니다. */
        struct SceneStatistics
        {
            uint32                           _objectCount{ 0 };
            uint32                           _rootCount{ 0 };
            uint32                           _componentCount{ 0 };
            vector<ComponentDistributionRow> _listDistribution;
        };

        /**
         * @brief 씬의 오브젝트와 컴포넌트를 세고 타입별 분포를 만듭니다.
         * @details 리플렉션 TypeInfo 로 묶으므로 게임이 만든 컴포넌트도 테이블에 나오고, 한 오브젝트에 같은 타입이 여럿이면 그 수대로 센다
         *          ("Active Instances"). 엔진에 컴포넌트가 늘어나도 패널을 고칠 필요가 없다.
         */
        static SceneStatistics collectSceneStatistics( GameObjectManager* pManager );
    };
} // namespace sw::editor
