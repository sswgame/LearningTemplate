/**
 * @file FloatCurvePropertyDrawer.cpp
 * @brief `FloatCurve` 프로퍼티의 그리기 확장입니다 — 줄에는 미리보기, 누르면 팝업 편집기(`EditorCurveEditor`).
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorCurveEditor.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Curve/FloatCurve.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @class FloatCurvePropertyDrawer
         * @brief 편집기는 작업 사본을 고치고, 마칠 때(끌기를 놓음 · 메뉴 동작) 한 번 값에 입힌다 — 되돌리기 한 줄(그리드의 `applyPropertyTextAsEdit`).
         * @details 열린 팝업은 하나뿐이라 상태도 하나입니다. 끄는 중이 아니면 프레임마다 값에서 사본을 다시 떠서 Undo · 다른 편집이 바로 보입니다.
         */
        class FloatCurvePropertyDrawer final : public IInspectorProperty
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                FloatCurve* pCurve = prop.getValuePtr<FloatCurve>( instance.get() );
                if ( pCurve == nullptr )
                    return false;
                if ( EditorCurveEditor::drawPreview( "##curvePreview", *pCurve, ImGui::GetFrameHeight() * 2.0f ) )
                {
                    _state                = EditorCurveEditorState{};
                    _state._working       = *pCurve;
                    const ImGuiID popupID = ImGui::GetID( "CurveEditor" );
                    _openPopupID          = popupID;
                    EditorInputGUI::openPopup( "CurveEditor" ); // 같은 id(위의 GetID 와 같은 이름)
                }
                fixed_string<constant::kMaxBuffer128> mark;
                formatstring( mark.data(), mark.capacity(), "inspector.curve.%#", prop._name.c_str() );
                EditorSelfTestMarks::note( mark.c_str() );

                const ImGuiID popupID = ImGui::GetID( "CurveEditor" );
                // 창 가운데에 연다 — 인스펙터는 오른쪽 끝이라 누른 위치에 열면 편집기가 창 밖으로 나간다.
                ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f } );
                EditorChrome::setNextWindowExtent( EditorTextExtent::ofText( 85.0f, 19.0f ), false );
                if ( ImGui::BeginPopupEx( popupID, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar ) == false )
                    return true;
                if ( popupID != _openPopupID )
                {
                    _state          = EditorCurveEditorState{};
                    _state._working = *pCurve;
                    _openPopupID    = popupID;
                }
                ImGui::TextUnformatted( prop._name.c_str() );
                // 끄는 중 · 필드 입력 중이 아니면 값에서 다시 뜬다(Undo · 다른 곳의 편집이 편집기에 보인다).
                if ( _state._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
                    _state._working = *pCurve;
                if ( EditorCurveEditor::drawEditor( _state ) )
                {
                    const string text = SerializerUtil::formatValueText( ValueAddress{ &_state._working }, prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                    grid.applyPropertyTextAsEdit( instance, prop, text, "Edit Curve" );
                }
                ImGui::EndPopup();
                return true;
            }

        private:
            EditorCurveEditorState _state{};
            ImGuiID                _openPopupID{ 0 };
        };
    } // namespace

    SW_EDITOR_PROPERTY_DRAWER( FloatCurve, "FloatCurve", FloatCurvePropertyDrawer );
} // namespace sw::editor
