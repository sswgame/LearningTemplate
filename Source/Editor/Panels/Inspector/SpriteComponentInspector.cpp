/**
 * @file SpriteComponentInspector.cpp
 * @brief SpriteComponent 푸터에 스프라이트 클립 도구를 여는 버튼을 둡니다
 */
#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief SpriteComponent 전용 프리뷰 및 애셋 슬롯 */
        class SpriteComponentInspector final : public IInspectorComponent
        {
        public:
            void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRHIDevice*/ ) override
            {
                if ( ImGui::SmallButton( "Open Sprite Clip Tool" ) )
                    editor::getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( EditorAssetType::SpriteClip );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( SpriteComponent, SpriteComponentInspector );
} // namespace sw::editor
