#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class Component;
	class IRHIDevice;

	/**
	 * @class IComponentDrawer
	 * @brief 컴포넌트별 커스텀 인스펙터 GUI 확장을 위한 인터페이스
	 */
	class IComponentDrawer
	{
	public:
		virtual ~IComponentDrawer() = default;

		/**
		 * @brief 컴포넌트 헤더 영역 추가 UI
		 */
		virtual void drawHeader( Component* /*pComponent*/ ) {}

		/**
		 * @brief 컴포넌트 본문 UI를 직접 렌더링할지 여부
		 * @return true 반환 시 기본 리플렉션 프로퍼티 렌더링을 생략하고 커스텀 UI만 렌더링함
		 */
		virtual bool drawBody( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) { return false; }

		/**
		 * @brief 기본 리플렉션 프로퍼티 렌더링 이후에 추가로 표시할 푸터 UI (예: 전용 도구 버튼, 프리뷰 등)
		 */
		virtual void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) {}
	};
} // namespace sw
