#include "pch.h"

#include "Editor/SelfTest/EditorCurveProbeComponent.h"

namespace sw::editor
{
    EditorCurveProbeComponent::EditorCurveProbeComponent()
        : _curve{}
    {
        _curve.addKey( 0.0f, 0.0f );
        _curve.addKey( 1.0f, 1.0f );
    }
} // namespace sw::editor
