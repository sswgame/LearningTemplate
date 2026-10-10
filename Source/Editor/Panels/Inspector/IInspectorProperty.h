/**
 * @file IInspectorProperty.h
 * @brief 리플렉션 프로퍼티 타입별 인스펙터 편집 UI
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /** @brief 프로퍼티 타입 하나의 인스펙터 위젯 */
    class SW_EDITOR_API IInspectorProperty
    {
    public:
        IInspectorProperty()                                       = default;
        virtual ~IInspectorProperty()                              = default;
        IInspectorProperty( const IInspectorProperty& )            = delete;
        IInspectorProperty& operator=( const IInspectorProperty& ) = delete;

        /**
         * @brief 프로퍼티 UI를 그립니다.
         * @return 이 구현이 프로퍼티를 **처리했으면** true (값이 바뀌었는지가 아닙니다). false 면 부르는 쪽이
         *         enum · 컨테이너 · 중첩 구조체 같은 일반 경로로 넘어갑니다.
         * @note 값 변경 통지는 구현이 하지 않습니다. InspectorPanel::drawPropertyWidget 이
         *       ImGui 편집 플래그로 한곳에서 판정합니다.
         */
        virtual bool draw( void* pInstance, const PropertyInfo& prop ) = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPropertyDrawerRegistration
     * @brief 프로퍼티 타입 하나의 그리기 확장 등록 줄입니다(유니티 PropertyDrawer 에 해당). id 는 리플렉션 `_typeName` 과 같은 타입 이름입니다.
     * @details 확장 모듈과 게임이 자기 값 타입에 위젯을 다는 길입니다. 내장 타입(`ReflectBuiltins.xxx`)과 이름이 같으면 등록 줄이 이깁니다.
     */
    struct EditorPropertyDrawerRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "propertydrawer";

        unique_ptr<IInspectorProperty> ( *_pCreate )();
    };

    /** @brief 등록 줄이 가리키는 그리기 생성 함수입니다. */
    template <typename TDrawer>
    unique_ptr<IInspectorProperty> createInspectorProperty()
    {
        return make_unique<TDrawer>();
    }
} // namespace sw::editor

/** @brief 프로퍼티 타입 그리기를 등록합니다. 예: `SW_EDITOR_PROPERTY_DRAWER( FloatCurve, "FloatCurve", FloatCurvePropertyDrawer );` */
#define SW_EDITOR_PROPERTY_DRAWER( name, pTypeName, TDrawer )                                                    \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPropertyDrawerRegistration, PropertyDrawer_##name, { pTypeName, 0 }, \
                        &::sw::editor::createInspectorProperty<TDrawer> )
