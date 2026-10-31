#include "pch.h"

#include "Engine/Automation/Runner/AutomationStepTimeline.h"

namespace sw
{
    AutomationStepTimeline::AutomationStepTimeline()
        : _listPendingScreenshot{}
        , _currentStepKind{}
        , _clockHolderName{}
        , _exitDeadlineSeconds{ 0.0 }
        , _loadDeadlineSeconds{ 0.0 }
        , _wallStartSeconds{ 0.0 }
        , _wallEndSeconds{ 0.0 }
        , _lastFrameEndSeconds{ 0.0 }
        , _clockHoldStartSeconds{ 0.0 }
        , _nextStepIndex{ 0 }
        , _frameIndex{ 0 }
        , _currentStepNumber{ 0 }
        , _waitFrameCount{ 0 }
        , _screenshotRequestedCount{ 0 }
        , _screenshotCompletedCount{ 0 }
        , _assetSettledFrameCount{ 0 }
        , _pHeldStepState{}
        , _pHeldStateStep{ nullptr }
        , _holdFrameCount{ 0 }
        , _clockHoldFrameCount{ 0 }
        , _seenFrameCount{ 0 }
        , _bClockHeld{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AutomationStepTimeline::requestScreenshot( string path )
    {
        _listPendingScreenshot.push_back( std::move( path ) );
        ++_screenshotRequestedCount;
    }

    bool AutomationStepTimeline::takePendingScreenshotPath( string& outPath )
    {
        if ( _listPendingScreenshot.empty() )
            return false;
        outPath = _listPendingScreenshot.front();
        _listPendingScreenshot.erase( _listPendingScreenshot.begin() );
        return true;
    }
} // namespace sw
