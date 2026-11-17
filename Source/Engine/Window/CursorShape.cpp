#include "pch.h"

#include "Engine/Window/CursorShape.h"

namespace sw
{
    const utf8* CursorShapeUtil::toCSSName( CursorShape shape )
    {
        switch ( shape )
        {
            case CursorShape::Arrow:
                return "default";
            case CursorShape::Hand:
                return "pointer";
            case CursorShape::TextBeam:
                return "text";
            case CursorShape::ResizeHorizontal:
                return "ew-resize";
            case CursorShape::ResizeVertical:
                return "ns-resize";
            case CursorShape::NotAllowed:
                return "not-allowed";
        }
        return "default"; // 열거 밖의 값(깨진 메모리)은 기본 화살표다
    }
} // namespace sw
