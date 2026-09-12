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
 * @note 프리미티브 등록부와 달리 **더티 표시도 인덱스도 없다.** 빛은 몇 개뿐이라 제거가 선형
 *       탐색으로 충분하고, 렌더러는 매 프레임 값을 새로 읽으므로 "무엇이 바뀌었나"를 알 필요가 없다.
 *       빛이 많아지는 날(점광·스포트) 그때 프리미티브 쪽 구조를 따라가면 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    class DirectionalLightComponent;

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

    private:
        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<DirectionalLightComponent*> _listDirectional;
        /** @brief 목록을 지킵니다. 등록/해제는 드물고, 조회는 게임 스레드 한 곳입니다. */
        mutable mutex _mutex;
    };
} // namespace sw
