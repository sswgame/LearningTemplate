#include "pch.h"

#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Core/Math/AABB.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerToggles.h"

namespace sw::editor
{
    void EditorVisualizerDrawList::clear()
    {
        _listLine.clear();
        _listText.clear();
        _bDebugDrawGathered = false;
    }

    void EditorVisualizerDrawList::addBox( const AABB& box, const EditorVisualizerLine& prototype )
    {
        const float3 arrCorner[] = {
            {box._min._x, box._min._y, box._min._z},
            {box._max._x, box._min._y, box._min._z},
            {box._max._x, box._max._y, box._min._z},
            {box._min._x, box._max._y, box._min._z},
            {box._min._x, box._min._y, box._max._z},
            {box._max._x, box._min._y, box._max._z},
            {box._max._x, box._max._y, box._max._z},
            {box._min._x, box._max._y, box._max._z},
        };
        static constexpr uint32 kArrEdge[][2] = {
            {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7},
        };
        for ( const auto& [fromIndex, toIndex] : kArrEdge )
        {
            EditorVisualizerLine line = prototype;
            line._from                = arrCorner[fromIndex];
            line._to                  = arrCorner[toIndex];
            _listLine.push_back( line );
        }
    }

    void EditorVisualizerDrawList::addLoop( const float3* pArrPoint, uint32 pointCount, const EditorVisualizerLine& prototype )
    {
        if ( pArrPoint == nullptr || pointCount < kMinLoopPointCount )
            return;
        for ( uint32 index = 0; index < pointCount; ++index )
        {
            EditorVisualizerLine line = prototype;
            line._from                = pArrPoint[index];
            line._to                  = pArrPoint[( index + 1 ) % pointCount];
            _listLine.push_back( line );
        }
    }

    void EditorVisualizerLineSink::drawLine( const float3& from, const float3& to, const float4& color )
    {
        EditorVisualizerLine line{};
        line._from     = from;
        line._to       = to;
        line._color    = color;
        line._statSlot = _statSlot;
        _drawList.addLine( line );
    }

    void EditorVisualizerDrawList::gatherAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles )
    {
        clear();
        const uint32 count = EditorExtensionRegistry<EditorVisualizerRegistration>::getCount();
        for ( uint32 index = 0; index < count; ++index )
        {
            const EditorVisualizerRegistration& registration = EditorExtensionRegistry<EditorVisualizerRegistration>::getAt( index );
            if ( registration._pGather != nullptr && toggles.isOn( registration ) )
                registration._pGather( args, *this );
        }
    }
} // namespace sw::editor
