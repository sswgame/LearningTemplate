/**
 * @file SocketPartPool.h
 * @brief 소켓에 붙는 부품 오브젝트(무기 · 방패 · 투구 프리팹)의 씬 풀입니다. 외형이 부품을 지울 때 넣고, 스폰할 때 같은 에셋을 꺼냅니다.
 * @details 스켈레톤 하나가 사라지고 생길 때마다 부품 프리팹을 세우고 지우던 비용(프리팹 로드 · 컴포넌트 생성)을 없앤다. 쉬는 부품은 소켓에서 떼고
 *          (`SocketBindingComponent::release( Animated )` — 붙은 오브젝트의 자식이면 그 오브젝트와 함께 지워진다) 꺼 둔다(`GameObject::setActive( false )` —
 *          그리기 · 틱 · 물리 바디가 모두 빠진다). 꺼낼 때 켜고, 다시 붙이는 일은 부르는 쪽(외형 조립)이 한다.
 *          물리로 떨어진 부품(`ReleasedPhysics`)과 이미 지울 예정인 오브젝트는 받지 않는다 — 부르는 쪽이 지운다.
 *          언리얼의 Actor Pool · 유니티의 `ObjectPool<T>` 에 해당한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class SocketPartPool
     * @brief 씬마다 하나 있는 소켓 부품 풀입니다(파일 머리말 참고). 에셋 경로마다 쉬는 부품 목록을 둡니다.
     */
    class SW_API SocketPartPool final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SocketPartPool";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SocketPartPool( GameObjectManager& manager );

        SocketPartPool( const SocketPartPool& )            = delete;
        SocketPartPool& operator=( const SocketPartPool& ) = delete;

        /** @brief 프레임마다 할 일이 없습니다(죽은 핸들은 꺼낼 때 건너뛴다). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 목록을 버립니다. 오브젝트는 매니저가 지운다. */
        void onSceneCleared() override;

        /** @brief @p assetPath 의 쉬는 부품을 켜서 내줍니다. 없으면 nullptr 이고 부르는 쪽이 새로 스폰합니다. */
        [[nodiscard]] GameObject* takePart( const hashed_string& assetPath );
        /**
         * @brief 부품 @p object 를 @p assetPath 의 쉬는 목록에 넣습니다. 소켓에 붙어 있으면 떼고 끕니다.
         * @return 넣지 못하면(지울 예정 · 물리로 떨어짐 · 되돌아가는 중 · `SocketBindingComponent` 없음) false 이고 부르는 쪽이 지웁니다.
         */
        [[nodiscard]] bool storePart( GameObject& object, const hashed_string& assetPath );
        /** @brief @p assetPath 의 쉬는 부품 수입니다(진단과 테스트용, 지워진 것은 세지 않는다). */
        uint32 getRestingCount( const hashed_string& assetPath ) const;

        /**
         * @brief 이 씬에서 `takePart` 가 쉬는 부품을 내준 수 / 내주지 못한 수 / `storePart` 가 받은 수입니다(누적). 풀의 효과를 재는 값입니다 —
         *        비교는 `-gv_socketPartPool=0`(풀 끔)과 하고, 시간은 프로파일 구간 `GT.Appearance.syncParts`(부품 스폰이 든다)로 읽습니다.
         */
        uint32 getTakeHitCount() const { return _takeHitCount; }
        uint32 getTakeMissCount() const { return _takeMissCount; }
        uint32 getStoredCount() const { return _storedCount; }

    private:
        GameObjectManager*                                     _pManager;
        unordered_map<hashed_string, vector<GameObjectHandle>> _mapResting; ///< 에셋 경로마다 쉬는 부품이다
        uint32                                                 _takeHitCount;
        uint32                                                 _takeMissCount;
        uint32                                                 _storedCount;
    };
} // namespace sw
