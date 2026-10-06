#include "pch.h"

#include "Engine/UI/Core/UiEvents.h"

namespace sw
{
    UiReply UiReply::makeHandled()
    {
        UiReply reply{};
        reply._bHandled = SW_TRUE;
        return reply;
    }

    UiReply UiReply::makeUnhandled()
    {
        return UiReply{};
    }

    UiReply& UiReply::capturePointer()
    {
        _bCapturePointer = SW_TRUE;
        _bReleasePointer = SW_FALSE;
        return *this;
    }

    UiReply& UiReply::releasePointer()
    {
        _bReleasePointer = SW_TRUE;
        _bCapturePointer = SW_FALSE;
        return *this;
    }

    UiReply& UiReply::requestFocus( WidgetId widget )
    {
        _focusRequest = widget;
        return *this;
    }
} // namespace sw
