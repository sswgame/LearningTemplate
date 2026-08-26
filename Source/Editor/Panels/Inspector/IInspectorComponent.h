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

	/** @brief 컴포넌트 헤더/본문/푸터를 커스텀하는 인스펙터 */
	class IInspectorComponent
	{
	public:
		virtual ~IInspectorComponent() = default;

		/** @brief 컴포넌트 헤더 추가 UI */
		virtual void drawHeader( Component* /*pComponent*/ ) {}

		/**
		 * @brief 본문을 직접 그립니다.
		 * @return true면 기본 리플렉션 프로퍼티를 생략합니다.
		 */
		virtual bool drawBody( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) { return false; }

		/** @brief 기본 프로퍼티 이후 푸터 UI */
		virtual void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) {}
	};
} // namespace sw
