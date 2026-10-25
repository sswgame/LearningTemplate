#include "pch.h"

#include "Engine/Automation/AutomationRunTitle.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Log/Logger.h"

#include "Engine/Utility/EngineFrameObserver.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#include <algorithm>

namespace sw
{
    string AutomationRunTitleUtil::makeSummary( const AutomationRunTitleInput& input )
    {
        string summary;
        if ( input._scenarioName.empty() == false )
        {
            summary += input._scenarioName;
        }
        else
        {
            summary += "bench";
            if ( input._benchSummary.empty() == false )
            {
                summary += ' ';
                summary += input._benchSummary;
            }
        }
        if ( input._backendName.empty() == false )
        {
            summary += " \xC2\xB7 "; // " · "
            summary += input._backendName;
        }
        if ( input._scenarioName.empty() == false && input._stepKind.empty() == false )
        {
            utf8 arrStep[constant::kMaxBuffer64];
            formatstring( arrStep, constant::kMaxBuffer64, " \xC2\xB7 step %# ", input._stepNumber );
            summary += arrStep;
            summary += input._stepKind;
        }
        utf8 arrFrame[constant::kMaxBuffer64];
        formatstring( arrFrame, constant::kMaxBuffer64, " \xC2\xB7 f%#", input._frame );
        summary += arrFrame;
        return summary;
    }

    string AutomationRunTitleUtil::makeWindowTitle( const AutomationRunTitleInput& input )
    {
        string title;
        if ( input._baseTitle.empty() == false )
        {
            title += input._baseTitle;
            title += " \xE2\x80\x94 "; // " — "
        }
        title += makeSummary( input );
        return truncateUtf8( title, kMaxTitleBytes );
    }

    string AutomationRunTitleUtil::makeStepLogLine( string_view scenarioName, uint32 stepNumber, string_view stepKind, uint32 frame )
    {
        string line = "[Scenario] ";
        line += scenarioName;
        utf8 arrStep[constant::kMaxBuffer64];
        formatstring( arrStep, constant::kMaxBuffer64, " step %# ", stepNumber );
        line += arrStep;
        line += stepKind;
        utf8 arrFrame[constant::kMaxBuffer64];
        formatstring( arrFrame, constant::kMaxBuffer64, " frame %#", frame );
        line += arrFrame;
        return line;
    }

    string AutomationRunTitleUtil::makeBenchSummary( vector<string> listPair )
    {
        constexpr string_view kPrefix = "gv_bench";
        (void)VectorUtil::removeIf( listPair, []( const string& pair )
        { return string_view{ pair }.substr( 0, kPrefix.size() ) != kPrefix; } ); // 지운 개수는 쓰지 않는다 — 요약에는 남은 쌍만 든다
        std::sort( listPair.begin(), listPair.end() );
        string summary;
        for ( const string& pair : listPair )
        {
            if ( summary.empty() == false )
                summary += ' ';
            summary += pair;
        }
        return summary;
    }

    SW_LOG_CALLER( "Automation" );

    AutomationRunTitleTracker::AutomationRunTitleTracker()
        : _initialTitle{}
        , _lastBase{}
        , _benchSummary{}
        , _lastStepKind{}
        , _frame{ 0 }
        , _lastStepNumber{ 0 }
        , _bInitialized{ false }
        , _bRunShown{ false }
    {
    }

    AutomationRunTitleTracker::~AutomationRunTitleTracker() = default;

    void AutomationRunTitleTracker::onFrameEnd( const MainWindowSubsystem* pWindowHost, const CommandLineManager* pCommandLine, string_view backendName,
                                                const vector<IEngineFrameObserver*>& listObserver )
    {
        IWindow* const pWindow = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( _bInitialized == false )
        {
            _bInitialized = true;
            if ( pCommandLine != nullptr )
            {
                vector<string> listPair;
                for ( const string& name : pCommandLine->collectPendingGlobalNames() )
                {
                    string value;
                    if ( pCommandLine->findPendingGlobalValue( name, value ) == false )
                        continue;
                    string pair = name;
                    pair += '=';
                    pair += value;
                    listPair.push_back( std::move( pair ) );
                }
                _benchSummary = AutomationRunTitleUtil::makeBenchSummary( std::move( listPair ) );
            }
            if ( _benchSummary.empty() == false )
                SW_LOG_INFO( "[Bench] backend %# args %#", string{ backendName }.c_str(), _benchSummary.c_str() );
            if ( pWindow != nullptr )
                _initialTitle = pWindow->getTitle();
        }
        ++_frame;

        AutomationRunTitleInput input;
        string                  scenarioName;
        string                  stepKind;
        uint32                  stepNumber = 0;
        uint32                  frame      = _frame;
        bool                    bScenario  = false;
        for ( const IEngineFrameObserver* pObserver : listObserver )
        {
            bScenario = pObserver->tryGetRunStatus( scenarioName, stepKind, stepNumber, frame );
            if ( bScenario )
                break;
        }
        const bool bBench = bScenario == false && _benchSummary.empty() == false;
        if ( pWindow == nullptr )
            return;
        // 제목 주인은 이 함수 하나다 — 평소 제목은 호스트가 들고(에디터가 프레임마다 정한다), 정한 적이 없으면 창이 처음 가진 제목이다.
        const string& hostBaseTitle = pWindowHost->getBaseTitle();
        const string& baseTitle     = hostBaseTitle.empty() ? _initialTitle : hostBaseTitle;
        const bool    bBaseChanged  = baseTitle != _lastBase;
        if ( bScenario == false && bBench == false )
        {
            if ( _bRunShown || bBaseChanged )
            {
                pWindow->setTitle( baseTitle.c_str() ); // 시나리오가 없거나 끝났다 — 평소 제목 그대로다
                _lastBase = baseTitle;
                _lastStepKind.clear();
                _bRunShown = false;
            }
            return;
        }
        const bool bStepChanged = bScenario && ( stepNumber != _lastStepNumber || stepKind != _lastStepKind );
        const bool bRefreshDue  = _frame % AutomationRunTitleUtil::kTitleRefreshFrames == 0;
        const bool bUpToDate    = bStepChanged == false && bRefreshDue == false && bBaseChanged == false && _bRunShown;
        if ( bUpToDate )
            return;
        _lastBase           = baseTitle;
        input._baseTitle    = _lastBase;
        input._scenarioName = scenarioName;
        input._backendName  = backendName;
        input._stepKind     = stepKind;
        input._benchSummary = _benchSummary;
        input._stepNumber   = stepNumber;
        input._frame        = frame;
        _lastStepNumber     = stepNumber;
        _lastStepKind       = stepKind;
        _bRunShown          = true;
        pWindow->setTitle( AutomationRunTitleUtil::makeWindowTitle( input ).c_str() );
    }

    string AutomationRunTitleUtil::truncateUtf8( string_view text, uint32 maxBytes )
    {
        constexpr uint32 kEllipsisBytes = 3; // "…" = E2 80 A6
        if ( text.size() <= maxBytes )
            return string{ text };
        size_t cut = maxBytes > kEllipsisBytes ? maxBytes - kEllipsisBytes : 0;
        while ( cut > 0 && ( static_cast<uint8>( text[cut] ) & 0xC0 ) == 0x80 )
        {
            --cut; // 이어지는 바이트 위에서는 글자 중간이다
        }
        string result{ text.substr( 0, cut ) };
        result += "\xE2\x80\xA6";
        return result;
    }
} // namespace sw
