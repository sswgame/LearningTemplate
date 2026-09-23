/**
 * @file IInspectorComponent.h
 * @brief 컴포넌트 타입별 인스펙터 UI 확장
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class Component;
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief 컴포넌트의 헤더 · 본문 · 푸터를 바꿔 그리는 인스펙터 확장입니다. */
    class IInspectorComponent
    {
    public:
        virtual ~IInspectorComponent() = default;

        /** @brief 컴포넌트 헤더에 UI 를 더합니다. */
        virtual void drawHeader( Component* /*pComponent*/ ) {}

        /**
         * @brief 본문을 직접 그립니다.
         * @return true면 기본 리플렉션 프로퍼티를 생략합니다.
         */
        virtual bool drawBody( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) { return false; }

        /** @brief 기본 프로퍼티 뒤에 푸터 UI 를 그립니다. */
        virtual void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) {}
    };
} // namespace sw::editor
