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
        return EditorRegistry<EditorVisualizerRegistration>::getCount();
    }

    const EditorVisualizerRegistration& EditorViewportVisualizer::getAt( uint32 index )
    {
        return EditorRegistry<EditorVisualizerRegistration>::getAt( index );
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles )
    {
        if ( args._pDrawList == nullptr || args._pViewProj == nullptr || args._pListObject == nullptr )
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
} // namespace sw::editor
