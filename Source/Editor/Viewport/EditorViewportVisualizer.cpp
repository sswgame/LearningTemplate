/**
 * @file EditorViewportVisualizer.cpp
 * @brief 등록된 시각화를 마스크로 고르고 그립니다(시각화 자체는 `Viewport/Visualizers/` 의 각 파일)
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Core/Log/Logger.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorViewportVisualizer" );

    uint32 EditorViewportVisualizer::getCount()
    {
        const uint32 count = EditorRegistry<EditorVisualizerRegistration>::getCount();
        return count < kMaxVisualizerCount ? count : kMaxVisualizerCount;
    }

    const EditorVisualizerRegistration& EditorViewportVisualizer::getAt( uint32 index )
    {
        return EditorRegistry<EditorVisualizerRegistration>::getAt( index );
    }

    uint32 EditorViewportVisualizer::getDefaultMask()
    {
        if ( EditorRegistry<EditorVisualizerRegistration>::getCount() > kMaxVisualizerCount )
        {
            SW_LOG_ERROR( "%# viewport visualizers are registered but the mask holds %# - the rest are never drawn",
                          EditorRegistry<EditorVisualizerRegistration>::getCount(), kMaxVisualizerCount );
        }

        uint32 mask{ 0 };
        for ( uint32 index = 0; index < getCount(); ++index )
        {
            if ( getAt( index )._bDefaultOn )
                mask |= getMaskBit( index );
        }
        return mask;
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, uint32 visualizerMask )
    {
        if ( args._pDrawList == nullptr || args._pViewProj == nullptr || args._pListObject == nullptr )
            return;

        for ( uint32 index = 0; index < getCount(); ++index )
        {
            const EditorVisualizerRegistration& registration = getAt( index );
            if ( ( visualizerMask & getMaskBit( index ) ) == 0 )
                continue;
            if ( registration._pDraw != nullptr )
                registration._pDraw( args );
        }
    }
} // namespace sw::editor
