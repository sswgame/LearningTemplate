#include "pch.h"

#include "Editor/SelfTest/EditorCurveTestComponent.h"

namespace sw::editor
{
    EditorCurveTestComponent::EditorCurveTestComponent()
        : _curve{}
    {
        _curve.addKey( 0.0f, 0.0f );
        _curve.addKey( 1.0f, 1.0f );
    }
} // namespace sw::editor
