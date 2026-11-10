/**
 * @file IInspectorProperty.h
 * @brief 리플렉션 프로퍼티 타입별 인스펙터 편집 UI
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorExtensionRegistry.h"

namespace sw::editor
{
    struct PropertyValueRow;

    class PropertyGridModel;
} // namespace sw::editor

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
         * @return 이 구현이 프로퍼티를 **처리했으면** true (값이 바뀌었는지가 아닙니다). false 면 그리드가 값 행의 종류대로 그립니다.
         * @param row 그리는 프로퍼티의 값 행입니다(`PropertyGridModel::gather` 가 채운다) — 값은 사본(`_value`)이거나 값 글(`_valueText`)이고, 멤버 주소는 없습니다.
         * @param model 그리는 그리드의 편집 모델입니다(명령 한 길). 편집은 행 번호(`row._rowID`)로 명령을 부릅니다 — 위젯이 고친 내장 값은 `setRowValue` · `setRowNumber`,
         *              팝업 편집기 · 선택기 · 끌기를 놓을 때 한 번은 `setPropertyText`(통지 · 되돌리기를 모델이 맡는다, 언리얼 IPropertyHandle 에 해당).
         *              씬 · 모델 헤더는 보지 않습니다 — 씬을 읽어야 하는 값은 Model 폴더의 값 모음(`SceneObjectReferencePickerState` · 모델의 `collectComponentChoices`)이 만든다.
         * @note 위젯의 활성 ~ 해제를 되돌리기 한 단계로 묶는 일은 구현이 `InspectorPropertyUndo::trackRowEdit` 로 하고, 그 뒤에 쓰기 명령을 부릅니다(편집 전을 먼저 찍는다).
         */
        virtual bool draw( const PropertyValueRow& row, PropertyGridModel& model ) = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPropertyDrawerRegistration
     * @brief 프로퍼티 타입 하나의 그리기 확장 등록 줄입니다(유니티 PropertyDrawer 에 해당). id 는 리플렉션 `_typeName` 과 같은 타입 이름입니다.
     * @details 확장 모듈과 게임이 자기 값 타입에 위젯을 다는 길입니다. 내장 타입(`ReflectBuiltins.xxx`)과 이름이 같으면 등록 줄이 이깁니다.
     */
    struct EditorPropertyDrawerRegistration : EditorExtensionRegistration
    {
        SW_EDITOR_EXTENSION_KIND( "propertydrawer" );

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
