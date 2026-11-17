/**
 * @file DataPathPropertyDrawer.cpp
 * @brief `DataPath` 프로퍼티의 그리기 확장입니다 — 리소스 id 를 치거나 콘텐츠 브라우저에서 끌어 놓는 경로 칸.
 * @details 없던 때는 프로젝트 설정의 경로 칸 일곱이 "No inspector for DataPath" 로만 보여 고칠 수 없었다(uiaudit). 언리얼 Details 의
 *          FSoftObjectPath 칸 · 유니티 Inspector 의 오브젝트 칸처럼 글로 치기와 끌어 놓기를 함께 받는다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @class DataPathPropertyDrawer
         * @brief 칸을 떠나거나 끌어 놓을 때 한 번 값에 입힌다 — 되돌리기 한 줄(모델의 `setPropertyText`). 값은 값 행의 글(`_valueText` — 경로 그대로)이다.
         * @details 글을 치는 칸은 한 번에 하나라 버퍼도 하나다. 치는 중이 아닌 칸은 그리기 전에 값에서 버퍼를 다시 채운다(Undo · 다른 편집이 바로 보인다).
         */
        class DataPathPropertyDrawer final : public IInspectorProperty
        {
        public:
            bool draw( const PropertyValueRow& row, PropertyGridModel& model ) override
            {
                ImGui::PushID( row._propName.c_str() );
                if ( ImGui::GetActiveID() != ImGui::GetID( "##path" ) )
                    _buffer = row._valueText.c_str();
                ImGui::SetNextItemWidth( -1.0f );
                const EditorWidgets::HintFieldResult result =
                    EditorWidgets::drawAssetPathField( "##path", _buffer, SW_LOCKEY( "EdCommon", "DataPathHint" ), nullptr, nullptr );
                ImGui::PopID();
                if ( result._bDeactivatedAfterEdit && ( string_view( row._valueText ) != string_view( _buffer.c_str() ) ) )
                    model.setPropertyText( row._rowID, _buffer.c_str(), "Edit Path" );
                return true;
            }

        private:
            fixed_string<constant::buffer::kMax512> _buffer;
        };
    } // namespace

    SW_EDITOR_PROPERTY_DRAWER( DataPath, "DataPath", DataPathPropertyDrawer );
} // namespace sw::editor
