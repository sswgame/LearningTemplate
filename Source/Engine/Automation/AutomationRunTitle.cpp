#include "pch.h"

#include "Engine/Automation/AutomationRunTitle.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Log/Logger.h"

#include "Engine/EngineFrameObserver.h"
#include "Engine/Window/IWindowTitleTarget.h"

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
        return truncateUTF8( title, kMaxTitleBytes );
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

    namespace
    {
        /** @brief 재현 명령의 인자 규칙입니다 — 무엇을 버리고 무엇을 가리는지가 이 구조체 하나에 있습니다. */
        struct AutomationReproduceRules
        {
            /** @brief 보고 · 녹화 · 크래시 보고처럼 시나리오의 결과를 바꾸지 않고 사용자 쪽 경로 · 주소를 싣는 인자의 이름(앞의 `-` 없이)입니다. 재현 명령에서 뺀다. */
            static constexpr string_view kArrDroppedName[] = { "scenario-report", "scenario-record", "scenario-record-watches", "crash-reporter" };
            /** @brief 이름에 이 조각이 들어 있으면(대소문자 무시) 값이 비밀이다 — 이름은 남기고 값을 `REDACTED` 로 가린다. */
            static constexpr string_view kArrSecretFragment[] = { "token", "secret", "password", "passwd", "apikey", "api_key", "api-key", "credential", "auth", "license" };
            /** @brief 비밀 값을 대신하는 글입니다(셸에서 따옴표 없이 써도 뜻이 변하지 않는 글자만). */
            static constexpr string_view kRedactedValue = "REDACTED";
            /** @brief 백엔드를 고르는 인자의 이름입니다(원본에 하나라도 있으면 실행기가 백엔드를 덧붙이지 않는다). */
            static constexpr string_view kArrBackendName[] = { "dx11", "dx12", "vk", "gl", "gv_rhiBackend" };

            /** @brief `-key=value` · `--key` · `key=value` 에서 앞의 `-` 와 `=` 뒤를 뗀 이름입니다. */
            static string_view findName( string_view argument )
            {
                string_view name = argument;
                string_view value;
                (void)StringUtil::trySplitKeyValue( argument, name, value ); // `=` 가 없으면 인자 전체가 이름이다(`--key`)
                while ( name.empty() == false && name.front() == '-' )
                {
                    name.remove_prefix( 1 );
                }
                return name;
            }

            static bool isDropped( string_view name )
            {
                return std::find( std::begin( kArrDroppedName ), std::end( kArrDroppedName ), name ) != std::end( kArrDroppedName );
            }

            static bool isSecret( string_view name )
            {
                const string lower = StringUtil::toLower( string{ name }.c_str() );
                for ( const string_view fragment : kArrSecretFragment )
                {
                    if ( string_view{ lower }.find( fragment ) != string_view::npos )
                        return true;
                }
                return false;
            }

            /** @brief 인자 하나에 규칙을 적용합니다. 버리면 false 이고, 비밀 값은 가려서 @p outArgument 에 씁니다. */
            [[nodiscard]] static bool apply( string_view argument, string& outArgument )
            {
                const string_view name = findName( argument );
                if ( isDropped( name ) )
                    return false;
                const size_t equalPos = argument.find( '=' );
                if ( isSecret( name ) && equalPos != string_view::npos )
                {
                    outArgument = string{ argument.substr( 0, equalPos + 1 ) };
                    outArgument += kRedactedValue;
                    return true;
                }
                outArgument = string{ argument };
                return true;
            }

            /** @brief 원본 인자 가운데 이름이 @p name 인 것이 있는지 봅니다. */
            static bool hasName( const vector<string>& listArgument, string_view name )
            {
                for ( const string& argument : listArgument )
                {
                    if ( findName( argument ) == name )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    string AutomationRunTitleUtil::makeReproduceCommand( const AutomationReproduceInput& input )
    {
        using Rules                = AutomationReproduceRules;
        const bool bScenarioInArgs = Rules::hasName( input._listOriginalArgument, "scenario" );
        if ( bScenarioInArgs == false && input._scenarioArgument.empty() )
            return string{};
        // 공백이나 점이 든 인자는 감싼다 — PowerShell 이 `-scenario=a.b.xml` 을 점에서 쪼갠다.
        const auto appendArgument = []( string& inoutCommand, string_view argument )
        {
            inoutCommand += ' ';
            const bool bNeedsQuote = argument.find_first_of( " ." ) != string_view::npos;
            if ( bNeedsQuote )
                inoutCommand += '"';
            inoutCommand += argument;
            if ( bNeedsQuote )
                inoutCommand += '"';
        };
        string command = "App.exe";
        // 원본 인자를 그대로 옮긴다(버릴 것과 비밀 값만 규칙이 거른다). 아래는 원본에 없는 것만 실행기가 아는 값으로 채운다(세션 없이 돈 실행 · 기본 게임 · 실제 백엔드).
        for ( const string& original : input._listOriginalArgument )
        {
            string argument;
            if ( Rules::apply( original, argument ) )
                appendArgument( command, argument );
        }
        const auto hasInOriginal = [&]( string_view name )
        { return Rules::hasName( input._listOriginalArgument, name ); };
        if ( input._game.empty() == false && hasInOriginal( "game" ) == false )
            appendArgument( command, "-game=" + input._game );
        const bool bBackendInArgs = std::any_of( std::begin( Rules::kArrBackendName ), std::end( Rules::kArrBackendName ), hasInOriginal );
        if ( input._backendName.empty() == false && bBackendInArgs == false )
            appendArgument( command, "-gv_rhiBackend=" + input._backendName );
        if ( input._bEditor && hasInOriginal( "EnableEditor" ) == false )
            appendArgument( command, "-EnableEditor" );
        if ( bScenarioInArgs == false )
            appendArgument( command, "-scenario=" + input._scenarioArgument );
        if ( input._bKeepGoing && hasInOriginal( "scenario-keep-going" ) == false )
            appendArgument( command, "-scenario-keep-going" );
        for ( const string& extra : input._listExtraArgument )
        {
            string argument;
            if ( hasInOriginal( Rules::findName( extra ) ) == false && Rules::apply( extra, argument ) )
                appendArgument( command, argument );
        }
        return command;
    }

    string AutomationRunTitleUtil::makeBenchSummary( vector<string> listPair )
    {
        static constexpr string_view kPrefix = "gv_bench";
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

    void AutomationRunTitleTracker::reset()
    {
        // `clear` 만으로는 힙 버퍼가 남는다. 빈 글과 맞바꾸지 않는 것은 Debug STL 이 맞바꾼 글의 프록시를 새로 잡은 블록으로 남기기 때문이다.
        for ( string* pText : { &_initialTitle, &_lastBase, &_benchSummary, &_lastStepKind } )
        {
            pText->clear();
            pText->shrink_to_fit();
        }
        _frame          = 0;
        _lastStepNumber = 0;
        _bInitialized   = false;
        _bRunShown      = false;
    }

    void AutomationRunTitleTracker::onFrameEnd( IWindowTitleTarget* pTitleTarget, const CommandLineManager* pCommandLine, string_view backendName,
                                                const vector<IEngineFrameObserver*>& listObserver )
    {
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
            if ( pTitleTarget != nullptr )
                _initialTitle = pTitleTarget->getCurrentTitle();
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
        if ( pTitleTarget == nullptr )
            return;
        // 제목 주인은 이 함수 하나다 — 평소 제목은 호스트가 들고(에디터가 프레임마다 정한다), 정한 적이 없으면 창이 처음 가진 제목이다.
        const string& hostBaseTitle = pTitleTarget->getBaseTitle();
        const string& baseTitle     = hostBaseTitle.empty() ? _initialTitle : hostBaseTitle;
        const bool    bBaseChanged  = baseTitle != _lastBase;
        if ( bScenario == false && bBench == false )
        {
            if ( _bRunShown || bBaseChanged )
            {
                pTitleTarget->applyWindowTitle( baseTitle.c_str() ); // 시나리오가 없거나 끝났다 — 평소 제목 그대로다
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
        pTitleTarget->applyWindowTitle( AutomationRunTitleUtil::makeWindowTitle( input ).c_str() );
    }

    string AutomationRunTitleUtil::truncateUTF8( string_view text, uint32 maxBytes )
    {
        constexpr uint32 kEllipsisBytes = 3; // "…" = E2 80 A6
        if ( text.size() <= maxBytes )
            return string{ text };
        const size_t cut = StringUtil::floorToUTF8Boundary( text, maxBytes > kEllipsisBytes ? maxBytes - kEllipsisBytes : 0 );
        string       result{ text.substr( 0, cut ) };
        result += "\xE2\x80\xA6";
        return result;
    }
} // namespace sw
