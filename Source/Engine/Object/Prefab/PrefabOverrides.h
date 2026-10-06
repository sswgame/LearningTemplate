/**
 * @file PrefabOverrides.h
 * @brief 프리팹 인스턴스가 프리팹과 다른 점(덮어쓴 값 · 지운 컴포넌트 · 더한 컴포넌트)을 뽑고, 프리팹 위에 다시 얹습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class PrefabAsset;

    /**
     * @class PrefabOverrides
     * @brief 씬이 프리팹 인스턴스를 저장하는 형식입니다 — 프리팹 경로와 **덮어쓴 것만** 적습니다(언리얼 레벨의 블루프린트 인스턴스 ·
     *        유니티 `PrefabInstance` 의 `m_Modifications` 와 같은 모양).
     * @details 그래서 프리팹을 고치면 다음 로드에서 놓인 인스턴스에 퍼지고, 인스턴스가 덮어쓴 값은 그대로 남습니다.
     *
     *          형식(`<PrefabOverrides>`)은 네 종류의 자식을 둡니다.
     *          - `<Object _bActive="false"/>` — 오브젝트 자기 칸 가운데 프리팹과 다른 것. 이름(`_name`)은 엔티티가 들고 있어 넣지 않습니다.
     *          - `<Remove key="BoxCollider2DComponent#0"/>` — 프리팹에 있는데 인스턴스가 지운 컴포넌트.
     *          - `<Override key="SpriteComponent#0"><SpriteComponent _localPosition="1,0,0"/></Override>` — 프리팹 컴포넌트 가운데 값이 다른 칸만.
     *          - `<Add after="SceneComponent#0"><TagComponent .../></Add>` — 인스턴스가 더한 컴포넌트(전체 상태). `after` 는 인스턴스 목록에서 바로 앞에 있던
     *            물려받은 컴포넌트의 키이고, 얹을 때 그 뒤에 들어갑니다. 앞에 물려받은 것이 없으면 `after` 가 없고 맨 앞에, 그 키가 프리팹에서 사라졌으면 끝에 붙습니다.
     *
     *          키는 **프리팹 쪽** 컴포넌트의 키(`이름표#n` — `ComponentStableKey` 와 같은 규칙, 이름표가 없으면 타입 이름)입니다. 프리팹 컴포넌트의
     *          이름표를 바꾼 인스턴스는 그 컴포넌트를 지우고 새로 더한 것으로 적힙니다(언리얼도 상속한 컴포넌트의 이름은 바꾸지 못한다).
     *          비교는 같은 직렬화기가 쓴 두 상태 XML 의 칸 글(속성 값 · 자식 원소)을 견줍니다 — 저장될 값 그대로를 견주므로 부착 · 핸들 PROPERTY 도
     *          저장할 id 로 견줍니다(`ObjectSaveOptions`).
     */
    class SW_API PrefabOverrides
    {
    public:
        /** @brief 씬 엔티티 안에서 덮어쓴 것을 담는 원소 이름입니다. */
        static constexpr const utf8* kRootName = "PrefabOverrides";

        /**
         * @brief 프리팹의 기준 상태(원형 — 언리얼 CDO 자리)를 오브젝트 상태 XML 로 만듭니다.
         * @details 프리팹을 씬 밖 임시 매니저의 오브젝트에 읽어 다시 씁니다 — 저장된 본문(XML · JSON · 쿠킹본)의 형식과 무관하게 같은 글이 나오고,
         *          프리팹 본문에 남은 다른 오브젝트로의 부착 · 핸들은 적지 않습니다. 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool makeBaseState( const PrefabAsset& prefab, string& outStateXml );

        /**
         * @brief 인스턴스 상태와 기준 상태의 차이를 `<PrefabOverrides>` XML 로 만듭니다. 다른 점이 없으면 @p outOverrideXml 은 빈 글입니다.
         * @return 두 상태 가운데 하나라도 오브젝트 상태 XML 로 읽지 못하면 false 입니다(그때 부른 쪽은 전체 상태를 저장한다).
         */
        [[nodiscard]] static bool computeOverrides( string_view instanceStateXml, string_view baseStateXml, string& outOverrideXml );

        /**
         * @brief 기준 상태에 덮어쓴 것을 얹어 인스턴스의 오브젝트 상태 XML 을 만듭니다. @p overrideXml 이 비면 기준 상태 그대로입니다.
         * @param instanceName 비어 있지 않으면 루트의 `_name` 을 이것으로 씁니다(엔티티 이름).
         * @details 프리팹에서 사라진 컴포넌트를 가리키는 덮어쓴 값은 버리고 엔티티 이름과 함께 경고합니다 — 다음 저장에서 빠집니다(언리얼 · 유니티도 원형에 없는
         *          오버라이드는 버린다). 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool makeInstanceState( string_view baseStateXml, string_view overrideXml, string_view instanceName, string& outStateXml );
    };
} // namespace sw
