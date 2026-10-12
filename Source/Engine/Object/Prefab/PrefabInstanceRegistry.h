/**
 * @file PrefabInstanceRegistry.h
 * @brief 중첩 프리팹으로 스폰한 오브젝트가 어느 프리팹 인스턴스의 부품인지 기록하는 씬 서비스입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/ISceneService.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /** @brief 중첩 프리팹 부품 하나입니다(`PrefabInstanceRegistry` 의 항목). */
    struct PrefabNestedPart
    {
        uint64 _ownerObjectID{ 0 };     ///< 이 부품을 스폰한 프리팹 인스턴스의 루트 오브젝트
        uint64 _registrationOrder{ 0 }; ///< 기록한 순서(스폰 순서). 부모 부품이 자식 부품보다 작다
        string _prefabPath;             ///< 이 부품의 원본 프리팹 경로
    };
} // namespace sw

namespace sw
{
    /**
     * @class PrefabInstanceRegistry
     * @brief 프리팹 인스턴스가 스폰한 중첩 프리팹 부품(자식 오브젝트)의 레지스트리입니다. 씬(`GameObjectManager`)마다 하나 있습니다.
     * @details 부품은 바깥 프리팹이 로드할 때마다 다시 스폰하므로 씬 저장(`Scene::serializeToDocument`)이 엔티티로 적지 않습니다. 그 판정에 이 레지스트리를
     *          씁니다. 언리얼의 차일드 액터가 저장되지 않고 부모 컴포넌트가 다시 스폰하는 것과 같습니다.
     *
     *          항목은 오브젝트가 없어져도 지우지 않습니다. 에디터 되돌리기와 플레이 종료 복원은 지운 오브젝트를 **같은 id 로** 다시 만들므로, 그때 표시가
     *          남아 있어야 부품으로 판정됩니다. id 는 다시 쓰이지 않으므로 남은 항목이 다른 오브젝트를 가리키지 않습니다. 씬을 비울 때(`shutdown`) 모두 버립니다.
     */
    class SW_API PrefabInstanceRegistry final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();

        /** @brief @p manager 의 부품 레지스트리를 만듭니다(서비스 레지스트리가 매니저마다 부른다). */
        explicit PrefabInstanceRegistry( GameObjectManager& manager );

        PrefabInstanceRegistry( const PrefabInstanceRegistry& )            = delete;
        PrefabInstanceRegistry& operator=( const PrefabInstanceRegistry& ) = delete;

        /** @brief 프레임마다 할 일은 없습니다. */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 항목을 모두 버립니다. */
        void shutdown() override;

        /** @brief @p objectID 를 @p ownerObjectID 인스턴스의 부품으로 기록합니다. 이미 있으면 바꿉니다. */
        void registerNestedPart( uint64 objectID, uint64 ownerObjectID, string_view prefabPath );
        /** @brief 기록을 지웁니다. 없으면 아무것도 하지 않습니다. */
        void unregisterNestedPart( uint64 objectID );
        /** @brief @p objectID 의 부품 기록입니다. 부품이 아니면 nullptr 입니다. */
        const PrefabNestedPart* findNestedPart( uint64 objectID ) const;
        /**
         * @brief @p pObject 가 부품으로 기록되어 있고 아직 그 인스턴스 아래에 있으면 true 입니다.
         * @details 씬 컴포넌트가 없는 부품은 계층이 없으므로 인스턴스가 살아 있으면 부품입니다. 사용자가 다른 곳으로 옮긴 부품은 false 이고,
         *          씬은 그것을 자기 프리팹의 인스턴스로 저장합니다(유니티의 Unpack 과 같다).
         */
        bool isAttachedNestedPart( const GameObject* pObject ) const;
        /**
         * @brief @p ownerObjectID 인스턴스가 스폰한 부품의 오브젝트 id 를 기록 순서(스폰 순서)로 채웁니다(비우고 채웁니다).
         * @details 표 전체를 돈다 — 프리팹으로 저장하는 에디터 경로가 쓴다. 프레임마다 부르지 않는다.
         */
        void collectNestedPartsOf( uint64 ownerObjectID, vector<uint64>& outListObjectID ) const;
        /** @brief 기록된 부품 수입니다. */
        size_t getNestedPartCount() const { return _mapNestedPart.size(); }

    private:
        GameObjectManager*                      _pManager;         ///< 이 레지스트리를 가진 매니저(빌림)
        unordered_map<uint64, PrefabNestedPart> _mapNestedPart;    ///< 부품 오브젝트 id → 기록
        uint64                                  _nextRegistration; ///< 다음 기록에 줄 순서
    };
} // namespace sw
