/**
 * @file TagComponentInspector.cpp
 * @brief TagComponent 헤더 뒤에 태그를 칩으로 그립니다
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Engine/Object/Component/TagComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief TagComponent 전용 칩 스타일 */
        class TagComponentInspector final : public IInspectorComponent
        {
        public:
            void drawFooter( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
            {
                auto* pTagComp = static_cast<TagComponent*>( pComponent );
                if ( pTagComp == nullptr )
                    return;

                const vector<TagID>& listTag = pTagComp->getTags().getTags();
                for ( const TagID& tag : listTag )
                {
                    if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                    {
                        ImGui::SameLine();
                        EditorWidgets::drawChip( tag._pString, editor::style::kOk );
                    }
                }
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( TagComponent, TagComponentInspector );
} // namespace sw::editor
