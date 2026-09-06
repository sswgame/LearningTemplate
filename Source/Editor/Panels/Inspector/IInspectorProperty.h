/**
 * @file IInspectorProperty.h
 * @brief 리플렉션 프로퍼티 타입별 인스펙터 편집 UI
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /** @brief 프로퍼티 타입 하나의 인스펙터 위젯 */
    class IInspectorProperty
    {
    public:
        virtual ~IInspectorProperty() = default;

        /**
         * @brief 프로퍼티 UI를 그립니다.
         * @return 이 구현이 프로퍼티를 **처리했으면** true. 값이 바뀌었는지가 아니다 —
         *         false 면 호출부가 enum/컨테이너/중첩 구조체 같은 일반 경로로 넘어간다.
         * @note 값 변경 통지는 구현이 하지 않는다. InspectorPanel::drawPropertyWidget 이
         *       ImGui 편집 플래그로 한 곳에서 판정한다.
         */
        virtual bool draw( void* pInstance, const PropertyInfo& prop ) = 0;
    };
} // namespace sw::editor
