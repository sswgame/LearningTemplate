/**
 * @file EditorViewportVisualizer.cpp
 * @brief 등록된 시각화 가운데 켜진 것을 그립니다(시각화 자체는 `Viewport/Visualizers/` 의 각 파일)
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

namespace sw::editor
{
    uint32 EditorViewportVisualizer::getCount()
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getCount();
    }

    const EditorVisualizerRegistration& EditorViewportVisualizer::getAt( uint32 index )
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getAt( index );
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles )
    {
        const bool bMissingDrawInputs = args._pDrawList == nullptr || args._pViewProj == nullptr;
        if ( bMissingDrawInputs )
            return;

        for ( uint32 index = 0; index < getCount(); ++index )
        {
            const EditorVisualizerRegistration& registration = getAt( index );
            if ( toggles.isOn( registration ) == false )
                continue;
            if ( registration._pDraw != nullptr )
                registration._pDraw( args );
        }
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorVisualizerRegistration )
} // namespace sw::editor
