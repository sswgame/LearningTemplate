/**
 * @file TagComponentInspector.cpp
 * @brief TagComponent 헤더 뒤에 태그를 칩으로 그립니다
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief TagComponent 전용 칩 스타일 */
        class TagComponentInspector final : public IInspectorComponent
        {
        public:
            void drawFooter( Component* pComponent ) override
            {
                if ( pComponent == nullptr )
                    return;
                InspectorComponentEdit::collectTags( *pComponent, _listTag );
                for ( const string& tag : _listTag )
                {
                    ImGui::SameLine();
                    EditorWidgets::drawChip( tag.c_str(), SW_THEME_COLOR( "Signal", "Success" ) );
                }
            }

        private:
            vector<string> _listTag{}; ///< 프레임마다 다시 채운다(버퍼를 다시 쓴다)
        };
    } // namespace

    SW_EDITOR_INSPECTOR_FOR_TYPE( TagComponent, &InspectorComponentEdit::getTagComponentType, TagComponentInspector );
} // namespace sw::editor
