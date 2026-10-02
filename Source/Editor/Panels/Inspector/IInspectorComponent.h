/**
 * @file IInspectorComponent.h
 * @brief 컴포넌트 타입별 인스펙터 UI 확장
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Component;
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 컴포넌트 타입 하나의 인스펙터 확장입니다. 그 타입과 **하위 타입** 모두에 걸립니다(기반 → 파생 순서로 차례로 그립니다).
     * @details 언리얼 `IDetailCustomization` 의 자리입니다. 확장은 자기 구역을 더 그리고, 직접 그린 반사 프로퍼티만 감춥니다(`collectDrawnProperties`
     *          — 언리얼 `HideProperty`). 나머지 반사 프로퍼티는 인스펙터가 상속분까지 그립니다(`InspectorPropertyLayout`).
     */
    class IInspectorComponent
    {
    public:
        virtual ~IInspectorComponent() = default;

        /** @brief 컴포넌트 헤더에 UI 를 더합니다. */
        virtual void drawHeader( Component* /*pComponent*/ ) {}

        /** @brief 이 타입 단계의 구역을 그립니다(반사 프로퍼티 앞). 예전 `drawBody` 와 달리 반사 프로퍼티를 통째로 감추지 않습니다. */
        virtual void drawSection( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) {}

        /** @brief `drawSection` 이 직접 그리는 반사 프로퍼티 이름입니다. 인스펙터는 이것들을 반사 칸에서 빼고 그립니다. */
        virtual void collectDrawnProperties( vector<hashed_string>& /*outListName*/ ) const {}

        /** @brief 기본 프로퍼티 뒤에 푸터 UI 를 그립니다. */
        virtual void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) {}
    };
} // namespace sw::editor
