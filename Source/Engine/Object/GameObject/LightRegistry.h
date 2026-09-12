/**
 * @file LightRegistry.h
 * @brief 빛 컴포넌트의 등록부
 *
 * [왜 필요한가]
 * `PrimitiveRegistry` 와 같은 이유다 — **찾지 말고 등록받는다.** 그릴 것이 그랬듯 비추는 것도
 * 붙을 때 자기를 등록하면, 프레임 루프의 비용이 "씬의 오브젝트 수"가 아니라 "빛의 수"가 된다.
 *
 * 이것이 없을 때 `Scene::findActiveDirectionalLight` 는 매 프레임 **모든 GameObject** 를 돌며
 * `getComponent<DirectionalLightComponent>()` 를 물었고, 찾은 뒤에도 순회를 멈추지 않았다
 * (`forEachGameObject` 에는 중단이 없다). 큐브 20,000 개 벤치에서 그 한 줄이 게임 스레드
 * 프레임 7.6ms 중 **2.9ms** 를 썼다 — 빛은 하나였다.
 *
 * 등록부를 `GameObjectManager` 안에 두지 않은 이유도 `PrimitiveRegistry` 와 같다. 매니저는 이미
 * 저장소·컴포넌트 풀·팩토리·틱 웨이브를 들고 있어서, 능력을 따로 떼어 두면 컴포넌트가 자기가
 * 쓰는 것만 들고 있으면 된다.
 *
 * @note 더티 표시는 없다. 렌더러가 매 프레임 값을 새로 읽어 GPU 버퍼를 다시 채우므로 "무엇이
 *       바뀌었나" 를 알 필요가 없다 — 프리미티브와 달리 라이트는 원소가 64 바이트뿐이다.
 *       제거는 선형 탐색이다. 점광이 수백 개인 벤치에서도 제거는 씬을 내릴 때만 일어난다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    class DirectionalLightComponent;
    class PointLightComponent;
    class SpotLightComponent;

    /**
     * @class LightRegistry
     * @brief 등록된 빛 목록을 관리합니다.
     * @note 락 순서 주의 — `PrimitiveRegistry` 와 같이 이 클래스의 락은 항상 **가장 안쪽**이다.
     *       `GameObjectManager` 가 자기 락을 쥔 채 등록/해제를 부를 수 있으므로, 반대로 이 락을
     *       쥔 채 매니저 락을 잡으면 교착이 된다. 그래서 등록/해제는 여기서만 끝낸다.
     */
    class SW_API LightRegistry
    {
    public:
        /** @brief 빈 등록부를 만듭니다. */
        LightRegistry() = default;
        /** @brief 등록부를 비웁니다. 빛의 수명은 GameObject 가 쥡니다. */
        ~LightRegistry() = default;

        LightRegistry( const LightRegistry& )            = delete;
        LightRegistry& operator=( const LightRegistry& ) = delete;

        /** @brief 방향광을 등록합니다. 붙을 때 1회. 이미 등록됐으면 무시합니다. */
        void addDirectional( DirectionalLightComponent* pComp );
        /** @brief 방향광을 등록 해제합니다. 멱등입니다. */
        void removeDirectional( DirectionalLightComponent* pComp );

        /**
         * @brief 등록된 방향광 목록입니다. 소유하지 않습니다.
         * @details 활성 여부 판정(컴포넌트 활성·소유 오브젝트의 계층 활성)은 **부르는 쪽**이 한다 —
         *          `PrimitiveRegistry::getAll` 과 같은 규약이다. 등록부는 "무엇이 있나"만 안다.
         */
        const vector<DirectionalLightComponent*>& getAllDirectional() const { return _listDirectional; }

        /** @brief 점광을 등록합니다. 붙을 때 1회. 이미 등록됐으면 무시합니다. */
        void addPoint( PointLightComponent* pComp );
        /** @brief 점광을 등록 해제합니다. 멱등입니다. */
        void removePoint( PointLightComponent* pComp );

        /**
         * @brief 등록된 점광 목록입니다. 소유하지 않습니다.
         * @details 방향광과 같은 규약 — 활성 판정은 부르는 쪽이 한다.
         */
        const vector<PointLightComponent*>& getAllPoint() const { return _listPoint; }

        /** @brief 스포트라이트를 등록합니다. 붙을 때 1회. 이미 등록됐으면 무시합니다. */
        void addSpot( SpotLightComponent* pComp );
        /** @brief 스포트라이트를 등록 해제합니다. 멱등입니다. */
        void removeSpot( SpotLightComponent* pComp );

        /**
         * @brief 등록된 스포트라이트 목록입니다. 소유하지 않습니다.
         * @details 방향광·점광과 같은 규약 — 활성 판정은 부르는 쪽이 한다.
         */
        const vector<SpotLightComponent*>& getAllSpot() const { return _listSpot; }

    private:
        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<DirectionalLightComponent*> _listDirectional;
        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<PointLightComponent*> _listPoint;
        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<SpotLightComponent*> _listSpot;
        /** @brief 목록을 지킵니다. 등록/해제는 드물고, 조회는 게임 스레드 한 곳입니다. */
        mutable mutex _mutex;
    };
} // namespace sw
