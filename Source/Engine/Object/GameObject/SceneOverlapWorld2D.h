/**
 * @file SceneOverlapWorld2D.h
 * @brief 씬 하나의 겹침 월드 — AABB 질의 월드(`PhysicsWorld`), 바디를 맞출 2D 콜라이더 목록, 겹침 이벤트를 컴포넌트에 나눠 주기입니다.
 * @details `GameObjectManager` 가 소유만 하고(`ScenePhysics` · `PrimitiveRegistry` 와 같은 자리), 프레임마다 강체 물리 앞에서 `step` 을 한 번 부릅니다.
 *          강체(`ScenePhysics`)와 따로 돕니다 — 이쪽은 적분하지 않고 겹침 시작 · 끝만 냅니다(유니티 `Physics2D` 트리거, 언리얼 Begin/EndOverlap 의 자리).
 *
 *          콜라이더는 병렬 틱에서 제 바디를 맞추지 않습니다 — 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 보게 된다. 그래서 등록받아
 *          `step` 직전에 한 번에 맞춥니다. 질의(`getPhysicsWorld`)는 키트의 투사체 · 근접 판정 · 카메라 탐침 · 에디터 쓸기가 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    class BoxCollider2DComponent;
    class Component;
    class GameObjectManager;

    /** @class SceneOverlapWorld2D @brief 씬 하나의 겹침 월드입니다. 파일 머리말 참고. */
    class SW_API SceneOverlapWorld2D
    {
    public:
        SceneOverlapWorld2D();
        ~SceneOverlapWorld2D() = default;

        SceneOverlapWorld2D( const SceneOverlapWorld2D& )            = delete;
        SceneOverlapWorld2D& operator=( const SceneOverlapWorld2D& ) = delete;

        /** @brief AABB 질의 월드입니다(겹침 · 쓸기 · 레이어 표). */
        PhysicsWorld& getPhysicsWorld() { return _physicsWorld; }
        /** @brief AABB 질의 월드입니다. */
        const PhysicsWorld& getPhysicsWorld() const { return _physicsWorld; }

        /** @brief 바디를 맞출 콜라이더를 등록합니다(`BoxCollider2DComponent::onRegister`). 이미 있으면 아무것도 하지 않습니다. */
        void registerCollider( BoxCollider2DComponent* pCollider );
        /** @brief 콜라이더 등록을 풉니다. 멱등입니다. */
        void unregisterCollider( BoxCollider2DComponent* pCollider );
        /** @brief 등록된 콜라이더 목록입니다(순서 없음). 틱 밖에서 읽습니다 — 에디터 시각화가 씬 전체를 훑지 않고 이것을 봅니다. */
        const vector<BoxCollider2DComponent*>& getColliders() const { return _listCollider; }

        /**
         * @brief 콜라이더 바디를 한 번에 맞추고 월드를 진행한 뒤, 겹침 이벤트를 두 오브젝트의 켜진 컴포넌트에 나눠 줍니다. 게임 스레드, 틱 밖.
         * @details 틱 · 트랜스폼 적용이 끝난 뒤라 모든 콜라이더가 같은 프레임의 자리를 봅니다. 꺼진 콜라이더는 빠집니다(겹침이 끝난다).
         *          이벤트 처리가 콜라이더를 만들거나 지워도 됩니다 — 나눠 줄 이벤트와 대상 컴포넌트 목록은 베껴 둔 것을 돕니다.
         * @param manager 이벤트의 오브젝트 id 를 풀 매니저
         */
        void step( GameObjectManager& manager, float32 deltaTime );

    private:
        PhysicsWorld                    _physicsWorld;
        vector<BoxCollider2DComponent*> _listCollider;  ///< 콜라이더가 자기 자리(`_colliderIndex`)를 든다
        vector<PhysicsOverlapEvent>     _listDelivered; ///< 나눠 주는 동안 도는 이벤트 사본(할당 재사용)
        vector<Component*>              _listTarget;    ///< 한 오브젝트에 나눠 주는 동안 도는 컴포넌트 사본(할당 재사용)
    };
} // namespace sw
