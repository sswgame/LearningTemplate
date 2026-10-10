#include "pch.h"

#include "Engine/UI/Base/UIEvents.h"

namespace sw
{
    UIReply UIReply::makeHandled()
    {
        UIReply reply{};
        reply._bHandled = SW_TRUE;
        return reply;
    }

    UIReply UIReply::makeUnhandled()
    {
        return UIReply{};
    }

    UIReply& UIReply::capturePointer()
    {
        _bCapturePointer = SW_TRUE;
        _bReleasePointer = SW_FALSE;
        return *this;
    }

    UIReply& UIReply::releasePointer()
    {
        _bReleasePointer = SW_TRUE;
        _bCapturePointer = SW_FALSE;
        return *this;
    }

    UIReply& UIReply::requestFocus( WidgetId widget )
    {
        _focusRequest = widget;
        return *this;
    }
} // namespace sw
