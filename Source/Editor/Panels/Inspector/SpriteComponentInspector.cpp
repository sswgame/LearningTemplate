/**
 * @file SpriteComponentInspector.cpp
 * @brief SpriteComponent 푸터에 스프라이트 클립 도구를 여는 버튼을 둡니다
 */
#include "pch.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief SpriteComponent 전용 프리뷰 및 애셋 슬롯 */
        class SpriteComponentInspector final : public IInspectorComponent
        {
        public:
            void drawFooter( Component* /*pComponent*/ ) override
            {
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "OpenSpriteClipTool" ) ) )
                    InspectorComponentEdit::openSpriteClipTool();
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR_FOR_TYPE( SpriteComponent, &InspectorComponentEdit::getSpriteComponentType, SpriteComponentInspector );
} // namespace sw::editor
