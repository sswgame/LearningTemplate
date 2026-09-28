/**
 * @file LightRegistry.h
 * @brief 빛 컴포넌트의 등록부입니다.
 *
 * [왜 필요한가]
 * `PrimitiveRegistry` 와 같은 이유입니다. **찾지 말고 등록받습니다.** 그릴 것이 그랬듯 비추는 것도
 * 붙을 때 자기를 등록하면, 프레임 루프의 비용이 "씬의 오브젝트 수"가 아니라 "빛의 수"가 됩니다.
 *
 * 이것이 없을 때 `Scene::findActiveDirectionalLight` 는 매 프레임 **모든 GameObject** 를 돌며
 * `getComponent<DirectionalLightComponent>()` 를 물었고, 찾은 뒤에도 순회를 멈추지 않았습니다
 * (`forEachGameObject` 에는 중단이 없습니다). 큐브 20,000 개 벤치에서 그 한 줄이 게임 스레드
 * 프레임 7.6ms 중 **2.9ms** 를 썼습니다. 빛은 하나였습니다.
 *
 * 등록부를 `GameObjectManager` 안에 두지 않은 이유도 `PrimitiveRegistry` 와 같습니다. 매니저는 이미
 * 저장소 · 컴포넌트 풀 · 팩토리 · 틱 등록부를 들고 있어서, 능력을 따로 떼어 두면 컴포넌트가 자기가
 * 쓰는 것만 들고 있으면 됩니다.
 *
 * 종류(방향광 · 점광 · 스포트)마다 칸 하나입니다. 예전에는 종류마다 add · remove · getAll 이 한 벌씩(세 벌) 손으로 복사돼 있어 빛 종류
 * 하나를 더하면 여기만 세 자리였습니다. 지금은 `LightComponent` 가 자기 종류(`getLightType`)로 한 쌍의 함수를 부릅니다.
 *
 * @note 더티 표시는 없습니다. 렌더러가 매 프레임 값을 새로 읽어 GPU 버퍼를 다시 채우므로 "무엇이
 *       바뀌었나" 를 알 필요가 없습니다. 프리미티브와 달리 라이트는 원소가 64 바이트뿐입니다.
 *       제거는 선형 탐색입니다. 점광이 수백 개인 벤치에서도 제거는 씬을 내릴 때만 일어납니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class LightComponent;

    /**
     * @class LightRegistry
     * @brief 등록된 빛 목록을 종류별로 관리합니다.
     * @note 락 순서에 주의하십시오. `PrimitiveRegistry` 와 같이 이 클래스의 락은 항상 **가장 안쪽**입니다.
     *       `GameObjectManager` 가 자기 락을 쥔 채 등록/해제를 부를 수 있으므로, 반대로 이 락을
     *       쥔 채 매니저 락을 잡으면 교착이 됩니다. 그래서 등록/해제는 여기서만 끝냅니다.
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

        /** @brief 빛을 그 종류의 칸에 등록합니다. 붙을 때 한 번 부릅니다. 이미 등록됐거나 nullptr 이면 무시합니다. */
        void add( LightComponent* pLight );
        /** @brief 빛을 등록 해제합니다. 멱등입니다. */
        void remove( LightComponent* pLight );

        /**
         * @brief 종류 @p lightType(`shaderslot::kLightType*`)의 등록된 빛 목록입니다. 소유하지 않습니다.
         * @details 활성 여부 판정은 **부르는 쪽**이 합니다(`Component::isActive` 가 소유 오브젝트의 계층 활성까지 봅니다).
         *          `PrimitiveRegistry::getAll` 과 같은 규약입니다. 등록부는 "무엇이 있나"만 압니다. 종류 번호가 방향광부터라(0)
         *          종류 순서로 돌면 방향광이 앞에 옵니다 — "그림자를 드리우는 첫 방향광" 과 수집 상한에서의 앞쪽 자르기가 거기 기댑니다.
         */
        const vector<LightComponent*>& getAll( uint32 lightType ) const;

    private:
        /** @brief 종류마다 등록된 빛입니다. 소유하지 않습니다(수명은 GameObject 가 쥡니다). */
        array<vector<LightComponent*>, shaderslot::kLightTypeCount> _arrListLight;
        /** @brief 목록을 지킵니다. 등록/해제는 드물고, 조회는 게임 스레드 한 곳입니다. */
        mutable mutex _mutex;
    };
} // namespace sw
