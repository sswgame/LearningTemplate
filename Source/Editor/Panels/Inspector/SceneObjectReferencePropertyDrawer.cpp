/**
 * @file SceneObjectReferencePropertyDrawer.cpp
 * @brief `SceneObjectReference` 프로퍼티의 그리기 확장입니다 — 올라와 있는 씬들의 오브젝트에서 하나를 고르는 콤보(다른 씬 · 프리팹 파일의 오브젝트 참조).
 * @details 미리보기와 고를 줄은 Model `SceneObjectReferencePickerState` 가 값으로 모은다(씬 매니저를 읽는 일은 거기 있다). 고르면 그 줄의 값 글을 편집 모델의 한 길
 *          (`PropertyGridModel::setPropertyText`)로 쓴다. 지금 값이 올라와 있지 않은 씬을 가리키면 정상 상태(하위 레벨 · 셀이 내려감)로, 씬은 있는데
 *          오브젝트가 없으면 결함으로 미리보기에 색을 달리해 알린다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"
#include "Editor/Panels/Inspector/Model/SceneObjectReferencePickerState.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        class SceneObjectReferencePropertyDrawer final : public IInspectorProperty
        {
        public:
            bool draw( const PropertyValueRow& row, PropertyGridModel& model ) override
            {
                if ( SceneObjectReferencePickerState::gatherPreview( model, row._rowID, _choices ) == false )
                    return false;
                const bool bWarn = _choices._bWarn == SW_TRUE;
                if ( bWarn )
                    EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
                // 씬 오브젝트 목록은 길다(기본 높이는 8 줄이라 이름 순으로 뒤에 있는 오브젝트가 스크롤 아래로 가려져 눌 수 없다) — 큰 높이로 연다.
                const bool bOpen = EditorWidgets::beginCombo( "##sceneObjectReference", _choices._preview.c_str(), ImGuiComboFlags_HeightLarge );
                if ( bWarn )
                    EditorThemeUtil::popTextColor();
                if ( bOpen == false )
                    return true;

                const string* pPickedText = nullptr;
                const bool    bSet        = _choices._currentText != _choices._noneText;
                if ( ImGui::Selectable( "(none)", bSet == false ) && bSet )
                    pPickedText = &_choices._noneText;
                EditorSelfTestMarks::record( "inspector.sceneref.none" ); // 시나리오가 누른다 — 콤보를 연 프레임에만 그려진다
                SceneObjectReferencePickerState::gatherScenes( model, row._rowID, _choices );
                for ( const SceneObjectReferenceScene& scene : _choices._listScene )
                {
                    const string* pSceneText = drawSceneChoices( scene, _choices._currentText );
                    if ( pSceneText != nullptr )
                        pPickedText = pSceneText;
                }
                ImGui::EndCombo();
                if ( pPickedText != nullptr )
                    model.setPropertyText( row._rowID, *pPickedText, "Pick Scene Object" );
                return true;
            }

        private:
            /** @brief 씬 하나의 오브젝트 줄을 보이고, 지금 값과 다른 줄을 골랐으면 그 값 글을 돌려줍니다. */
            static const string* drawSceneChoices( const SceneObjectReferenceScene& scene, const string& currentText )
            {
                if ( scene._title.empty() )
                {
                    ImGui::TextDisabled( "(unsaved scene: save it to reference its objects)" );
                    return nullptr;
                }
                const string* pPickedText = nullptr;
                ImGui::SeparatorText( scene._title.c_str() );
                for ( const SceneObjectReferenceChoice& choice : scene._listChoice )
                {
                    ImGui::PushID( static_cast<int32>( choice._objectID ) );
                    const bool bCurrent = choice._valueText == currentText;
                    if ( ImGui::Selectable( choice._label.c_str(), bCurrent ) && bCurrent == false )
                        pPickedText = &choice._valueText;
                    ImGui::PopID();
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::record( ( "inspector.sceneref.choice." + choice._label ).c_str() );
                }
                return pPickedText;
            }

        private:
            SceneObjectReferencePickerState _choices{}; ///< 그리는 프레임의 값 — 벡터의 저장 공간을 다시 쓴다
        };
    } // namespace

    SW_EDITOR_PROPERTY_DRAWER( SceneObjectReference, "SceneObjectReference", SceneObjectReferencePropertyDrawer );
} // namespace sw::editor
