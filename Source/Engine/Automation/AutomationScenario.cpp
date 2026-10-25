#include "pch.h"

#include "Engine/Automation/AutomationScenario.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Resource/Pack/ResourcePakManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AutomationScenarioInternal
        {
            static constexpr float32 kDefaultFixedDelta         = 1.0f / 60.0f;
            static constexpr uint32  kDefaultTimeoutFrames      = 3600;
            static constexpr uint32  kDefaultStartTimeoutFrames = 1200;

            /**
             * @brief 파일을 찾지 못한 이유를 막힌 단계(`[read]`)와 함께 적습니다. 리소스 ID 가 안 풀리는 원인(리소스 루트 · 작업 폴더 · 낱개 파일 금지 구성)이 로그 한 줄에 보이게 한다.
             */
            [[nodiscard]] static string describeReadFailure( string_view path )
            {
                const string loosePath = ResourcePaths::findLooseFile( path ).getDiskPath();
                const bool   bPackOnly = engine::areEngineServicesBound() && ResourceIO::getPackManager().isAllowLooseFiles() == false;
                return "[read] not found (resource root '" + ResourcePaths::getRootFolderPath() + "', working folder '" + FileUtil::getCurrentPath() + "', " +
                       ( loosePath.empty() ? string( "loose file unresolved" ) : "loose file '" + loosePath + "' is not a file" ) +
                       ( bPackOnly ? ", this build reads packs only and the id is in none of them)" : ", the id is in none of the mounted packs)" );
            }

            /** @brief 음이 아닌 정수 속성을 읽습니다. 형식이 틀리면 false 와 이유. */
            [[nodiscard]] static bool readUint( const utf8* pValue, const utf8* pName, uint32& outValue, string& outReason )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( string_view{ pValue }, value ) == false || value < 0 )
                {
                    outReason = string( pName ) + " must be a non-negative integer, got '" + pValue + "'";
                    return false;
                }
                outValue = static_cast<uint32>( value );
                return true;
            }

            /** @brief 불리언 속성을 읽습니다(철자 표는 `StringUtil::tryParseBool`). 읽지 못하면 false 와 이유(값은 false 로 둔다). */
            [[nodiscard]] static bool readBool( const utf8* pName, const utf8* pValue, bool& outValue, string& outReason )
            {
                outValue       = false;
                const bool bOk = StringUtil::tryParseBool( string_view{ pValue }, outValue );
                if ( bOk == false )
                    outReason = string( pName ) + " must be a boolean (true or false), got '" + pValue + "'";
                return bOk;
            }

            /** @brief 음이 아닌 초를 프레임으로 바꿉니다(고정 프레임 시간으로 반올림). 형식이 틀리면 false 와 이유. */
            [[nodiscard]] static bool readSeconds( const utf8* pValue, const utf8* pName, float32 fixedDelta, uint32& outFrames, string& outReason )
            {
                float32 seconds = 0.0f;
                if ( StringUtil::parseFloat( string_view{ pValue }, seconds ) == false || seconds < 0.0f )
                {
                    outReason = string( pName ) + " must be a non-negative number of seconds, got '" + pValue + "'";
                    return false;
                }
                outFrames = static_cast<uint32>( seconds / fixedDelta + 0.5f );
                return true;
            }

            /**
             * @brief `<At>` 의 시점을 프레임으로 읽습니다. `frame`(절대 프레임) · `time`(시작부터 초) · `after`(앞 `<At>` 에서 프레임) · `afterSeconds`(앞 `<At>` 에서 초) 중 정확히 하나입니다.
             * @details 상대 시점은 연속 동작("점프하고 0.2 초 뒤 이동")을 프레임 셈 없이 적게 한다. 초는 시나리오의 고정 프레임 시간으로 바꾼다.
             */
            [[nodiscard]] static bool readAtFrame( const XMLNode& atNode, float32 fixedDelta, uint32 previousFrame, uint32& outFrame, string& outReason )
            {
                uint32 timingCount = 0;
                for ( XMLAttribute attribute = atNode.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const string_view name   = attribute.getName();
                    const utf8*       pText  = attribute.getValue();
                    uint32            frames = 0;
                    bool              bOk    = true;
                    if ( name == "frame" )
                    {
                        bOk      = readUint( pText, "frame", frames, outReason );
                        outFrame = frames;
                    }
                    else if ( name == "time" )
                    {
                        bOk      = readSeconds( pText, "time", fixedDelta, frames, outReason );
                        outFrame = frames;
                    }
                    else if ( name == "after" )
                    {
                        bOk      = readUint( pText, "after", frames, outReason );
                        outFrame = previousFrame + frames;
                    }
                    else if ( name == "afterSeconds" )
                    {
                        bOk      = readSeconds( pText, "afterSeconds", fixedDelta, frames, outReason );
                        outFrame = previousFrame + frames;
                    }
                    else
                    {
                        outReason = "unknown <At> attribute '" + string( name ) + "' (frame · time · after · afterSeconds)";
                        return false;
                    }
                    if ( bOk == false )
                        return false;
                    ++timingCount;
                }
                if ( timingCount != 1 )
                {
                    outReason = "<At> needs exactly one of frame=\"N\" · time=\"seconds\" · after=\"N\" · afterSeconds=\"seconds\"";
                    return false;
                }
                return true;
            }

            /**
             * @brief `<Launch variable="gv_…" value="…"/>` 하나를 읽어 @p inoutList 에 더합니다.
             * @details 값은 명령줄 토큰 하나가 되므로 공백 · 따옴표를 받지 않는다. 같은 변수를 두 번 적으면 어느 쪽이 이기는지 모호하므로 오류다.
             */
            [[nodiscard]] static bool readLaunchVariable( const XMLNode& node, vector<AutomationAttribute>& inoutList, string& outReason )
            {
                AutomationAttribute entry{};
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const string_view name = attribute.getName();
                    if ( name == "variable" )
                    {
                        entry._name = attribute.getValue();
                    }
                    else if ( name == "value" )
                    {
                        entry._value = attribute.getValue();
                    }
                    else
                    {
                        outReason = "unknown attribute '" + string( name ) + "' (variable · value)";
                        return false;
                    }
                }
                const bool bWellFormed = StringUtil::startsWith( entry._name, "gv_" ) && entry._value.empty() == false && entry._value.find_first_of( " \t\"'" ) == string::npos;
                if ( bWellFormed == false )
                {
                    outReason = "needs variable=\"gv_…\" and a value without spaces or quotes, got variable='" + entry._name + "' value='" + entry._value + "'";
                    return false;
                }
                for ( const AutomationAttribute& declared : inoutList )
                {
                    if ( declared._name == entry._name )
                    {
                        outReason = entry._name + " is declared twice";
                        return false;
                    }
                }
                inoutList.push_back( std::move( entry ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const string* AutomationStep::findAttribute( string_view name ) const
    {
        for ( const AutomationAttribute& attribute : _listAttribute )
        {
            if ( string_view{ attribute._name } == name )
                return &attribute._value;
        }
        return nullptr;
    }

    string AutomationStep::describe() const
    {
        return "frame " + to_string( _frameIndex ) + " <" + _kind + "> #" + to_string( _orderInFile + 1 );
    }

    bool AutomationStep::hasOnlyAttributes( std::initializer_list<string_view> listAllowed, string& outError ) const
    {
        for ( const AutomationAttribute& attribute : _listAttribute )
        {
            bool bKnown = false;
            for ( const string_view allowed : listAllowed )
            {
                bKnown = bKnown || string_view{ attribute._name } == allowed;
            }
            if ( bKnown == false )
            {
                outError = describe() + ": unknown attribute '" + attribute._name + "'";
                return false;
            }
        }
        return true;
    }

    bool AutomationStep::hasRequiredAttributes( std::initializer_list<string_view> listAllowed, std::initializer_list<string_view> listRequired, string& outError ) const
    {
        if ( hasOnlyAttributes( listAllowed, outError ) == false )
            return false;
        for ( const string_view required : listRequired )
        {
            if ( findAttribute( required ) == nullptr )
            {
                outError = describe() + ": needs " + string( required ) + "=\"…\"";
                return false;
            }
        }
        return true;
    }

    const string* AutomationStep::requireAttribute( string_view name, string& outError ) const
    {
        const string* pValue = findAttribute( name );
        if ( pValue == nullptr )
            outError = describe() + ": needs " + string( name ) + "=\"…\"";
        return pValue;
    }

    AutomationScenario::AutomationScenario()
        : _listStep{}
        , _testOptions{}
        , _name{}
        , _sourcePath{}
        , _fixedDelta{ AutomationScenarioInternal::kDefaultFixedDelta }
        , _wallTimeoutSeconds{ kDefaultWallTimeoutSeconds }
        , _timeoutFrames{ AutomationScenarioInternal::kDefaultTimeoutFrames }
        , _startTimeoutFrames{ AutomationScenarioInternal::kDefaultStartTimeoutFrames }
        , _startCondition{ AutomationStartCondition::ScenePlaying }
        , _inputMode{ VirtualInputMode::Exclusive }
    {
    }

    bool AutomationScenario::loadFromPath( string_view path, string& outError )
    {
        XMLDocument doc;
        if ( doc.loadResource( path ) == false )
        {
            // 단계를 가른다 — 읽기(`[read]`, 리소스 ID 가 안 풀린다)와 XML 문법(`[xml]`)은 고치는 곳이 다르다. 형식 검사(`parse`)의 오류는 `[scenario]` 로 붙는다.
            const bool bReadable = ResourceIO::hasResource( path );
            outError             = bReadable ? "[xml] " + doc.getLastError() : AutomationScenarioInternal::describeReadFailure( path );
            return false;
        }
        if ( parse( doc.saveToString(), path, outError ) == false )
        {
            outError = "[scenario] " + outError;
            return false;
        }
        return true;
    }

    bool AutomationScenario::parseRootAttribute( string_view name, const utf8* pText, string& outReason )
    {
        using Internal = AutomationScenarioInternal;
        if ( name == "name" )
        {
            _name = pText;
            return true;
        }
        if ( name == "fixedDelta" )
        {
            const bool bOk = StringUtil::parseFloat( string_view{ pText }, _fixedDelta ) && _fixedDelta > 0.0f;
            if ( bOk == false )
                outReason = string( "fixedDelta must be a positive number, got '" ) + pText + "'";
            return bOk;
        }
        if ( name == "timeoutFrames" )
            return Internal::readUint( pText, "timeoutFrames", _timeoutFrames, outReason );
        if ( name == "startTimeoutFrames" )
            return Internal::readUint( pText, "startTimeoutFrames", _startTimeoutFrames, outReason );
        if ( name == "wallTimeoutSeconds" )
        {
            // 0 이나 음수로 상한을 끄지 못한다 — 끝나지 않는 시나리오가 테스트의 프로세스 시한(보고 없는 kill)까지 가는 것이 이 값이 막는 결함이다.
            // 상한은 시계 보류 시한(`AutomationTimeLimits`)보다 짧아야 한다 — 그 이상이면 프로세스 시한이 먼저 죽여 보고가 없다. 모든 시나리오 파일을 읽는 데이터 테스트가 이 자리에서 잡는다.
            const bool bParsed  = StringUtil::parseFloat( string_view{ pText }, _wallTimeoutSeconds );
            const bool bInRange = bParsed && _wallTimeoutSeconds > 0.0f && static_cast<float64>( _wallTimeoutSeconds ) < AutomationTimeLimits::kClockHoldTimeoutSeconds;
            if ( bInRange == false )
                outReason = string( "wallTimeoutSeconds must be a positive number of seconds below " ) + to_string( static_cast<uint32>( AutomationTimeLimits::kClockHoldTimeoutSeconds ) ) +
                            " (the clock-hold timeout), got '" + pText + "'";
            return bInRange;
        }
        if ( name == "startAfter" )
        {
            const string_view value = pText;
            if ( value == "ScenePlaying" )
                _startCondition = AutomationStartCondition::ScenePlaying;
            else if ( value == "Immediately" )
                _startCondition = AutomationStartCondition::Immediately;
            else
            {
                outReason = string( "unknown startAfter '" ) + pText + "' (ScenePlaying | Immediately)";
                return false;
            }
            return true;
        }
        if ( name == "isolated" )
            return Internal::readBool( "isolated", pText, _testOptions._bIsolated, outReason );
        if ( name == "requireForeground" )
            return Internal::readBool( "requireForeground", pText, _testOptions._bRequiresForeground, outReason );
        if ( name == "editor" )
            return Internal::readBool( "editor", pText, _testOptions._bRequiresEditor, outReason );
        if ( name == "input" )
        {
            const string_view value = pText;
            if ( value == "exclusive" )
                _inputMode = VirtualInputMode::Exclusive;
            else if ( value == "mixed" )
                _inputMode = VirtualInputMode::Mixed;
            else
            {
                outReason = string( "unknown input mode '" ) + pText + "' (exclusive | mixed)";
                return false;
            }
            return true;
        }
        outReason = "unknown <Scenario> attribute '" + string( name ) + "'";
        return false;
    }

    bool AutomationScenario::parseAtNode( const XMLNode& atNode, uint32& inoutOrderInFile, uint32& inoutPreviousFrame, string& outError )
    {
        using Internal = AutomationScenarioInternal;
        string reason;
        uint32 frameIndex = 0;
        if ( Internal::readAtFrame( atNode, _fixedDelta, inoutPreviousFrame, frameIndex, reason ) == false )
        {
            outError = _sourcePath + ": " + reason;
            return false;
        }
        inoutPreviousFrame     = frameIndex;
        const string frameText = to_string( frameIndex );
        for ( XMLNode stepNode = atNode.findChild(); stepNode.isValid(); stepNode = stepNode.findNextSibling() )
        {
            const string_view kind = stepNode.getName();
            if ( kind.empty() )
            {
                outError = _sourcePath + ": <At frame=\"" + frameText + "\"> holds text — only step elements may sit there";
                return false;
            }
            AutomationStep step{};
            step._kind        = string( kind );
            step._frameIndex  = frameIndex;
            step._orderInFile = inoutOrderInFile++;
            for ( XMLAttribute attribute = stepNode.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
            {
                AutomationAttribute value{};
                value._name  = attribute.getName();
                value._value = attribute.getValue();
                step._listAttribute.push_back( std::move( value ) );
            }
            _listStep.push_back( std::move( step ) );
        }
        return true;
    }

    bool AutomationScenario::parseChildren( const XMLNode& root, string& outError )
    {
        using Internal = AutomationScenarioInternal;
        string reason;
        uint32 orderInFile   = 0;
        uint32 previousFrame = 0;
        for ( XMLNode atNode = root.findChild(); atNode.isValid(); atNode = atNode.findNextSibling() )
        {
            const string_view elementName = atNode.getName();
            if ( elementName == "Load" )
            {
                if ( _testOptions._load._bDeclared )
                {
                    outError = _sourcePath + ": only one <Load> may sit under <Scenario>";
                    return false;
                }
                if ( parseLoad( atNode, reason ) == false )
                {
                    outError = _sourcePath + ": <Load> " + reason;
                    return false;
                }
                continue;
            }
            if ( elementName == "Scratch" )
            {
                AutomationScratchFolder folder;
                folder._sourceID  = string( atNode.getAttributeText( "from", false ) );
                folder._scratchID = string( atNode.getAttributeText( "as", false ) );
                if ( AutomationScratch::isValidFolder( folder, reason ) == false )
                {
                    outError = _sourcePath + ": " + reason;
                    return false;
                }
                _testOptions._listScratchFolder.push_back( std::move( folder ) );
                continue;
            }
            if ( elementName == "Launch" )
            {
                if ( Internal::readLaunchVariable( atNode, _testOptions._listLaunchVariable, reason ) == false )
                {
                    outError = _sourcePath + ": <Launch> " + reason;
                    return false;
                }
                continue;
            }
            if ( elementName != "At" )
            {
                outError = _sourcePath + ": only <Load>, <Scratch>, <Launch> and <At frame=\"N\"> may sit under <Scenario>, got <" + atNode.getName() + ">";
                return false;
            }
            if ( parseAtNode( atNode, orderInFile, previousFrame, outError ) == false )
                return false;
        }
        return true;
    }

    bool AutomationScenario::parse( string_view xmlText, string_view sourceName, string& outError )
    {
        *this       = AutomationScenario{};
        _sourcePath = sourceName.empty() ? string( "<memory>" ) : string( sourceName );

        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            outError = doc.getLastError();
            return false;
        }
        const XMLNode root = doc.getRoot( nullptr, false );
        if ( root.isValid() == false || string_view{ root.getName() } != "Scenario" )
        {
            outError = _sourcePath + ": the root element must be <Scenario>";
            return false;
        }

        // 루트 속성: 모르는 속성은 읽기 오류다(오타가 기본값으로 조용히 바뀌지 않게 한다).
        string reason;
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( parseRootAttribute( attribute.getName(), attribute.getValue(), reason ) == false )
            {
                outError = _sourcePath + ": " + reason;
                return false;
            }
        }
        if ( _name.empty() )
        {
            outError = _sourcePath + ": <Scenario> needs a name";
            return false;
        }

        if ( parseChildren( root, outError ) == false )
            return false;
        // 프레임 순이다(같은 프레임은 파일 순이며 안정 정렬이다).
        std::stable_sort( _listStep.begin(), _listStep.end(), []( const AutomationStep& lhs, const AutomationStep& rhs )
        {
            return lhs._frameIndex < rhs._frameIndex;
        } );
        // 부하 시나리오는 측정 창이 끝낸다 — `<Pass/>` 가 있으면 창이 닫히기 전에 프로세스가 끝나 보고가 없다.
        for ( const AutomationStep& step : _listStep )
        {
            if ( _testOptions._load._bDeclared && step._kind == "Pass" )
            {
                outError = _sourcePath + ": " + step.describe() + ": a scenario with <Load> ends when its measurement window closes - remove <Pass/>";
                return false;
            }
        }
        return true;
    }

    string AutomationTestOptions::makeLaunchArguments() const
    {
        string arguments;
        for ( const AutomationAttribute& variable : _listLaunchVariable )
        {
            arguments += ( arguments.empty() ? "-" : " -" ) + variable._name + "=" + variable._value;
        }
        return arguments;
    }

    bool AutomationScenario::parseLoad( const XMLNode& node, string& outReason )
    {
        using Internal                = AutomationScenarioInternal;
        _testOptions._load._bDeclared = true;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view name  = attribute.getName();
            const utf8*       pText = attribute.getValue();
            if ( name == "clients" )
            {
                if ( Internal::readUint( pText, "clients", _testOptions._load._clientCount, outReason ) == false )
                    return false;
            }
            else if ( name == "meshes" )
            {
                if ( Internal::readUint( pText, "meshes", _testOptions._load._meshCount, outReason ) == false )
                    return false;
            }
            else if ( name == "movers" )
            {
                if ( Internal::readUint( pText, "movers", _testOptions._load._moverCount, outReason ) == false )
                    return false;
            }
            else if ( name == "durationSeconds" || name == "rampUpSeconds" )
            {
                float32& outSeconds = name == "durationSeconds" ? _testOptions._load._durationSeconds : _testOptions._load._rampUpSeconds;
                if ( StringUtil::parseFloat( string_view{ pText }, outSeconds ) == false || outSeconds < 0.0f )
                {
                    outReason = string( name ) + " must be a non-negative number, got '" + pText + "'";
                    return false;
                }
            }
            else if ( name == "network" )
            {
                NetEmulationPresetTable presetTable;
                const DataPath&         presetPath = engine::getSubsystem<EngineDefaultAssets>()._netEmulationPresets;
                if ( presetTable.loadFromResource( presetPath.get() ) == false )
                {
                    outReason = string( "network presets could not be read (" ) + presetPath.get() + ")";
                    return false;
                }
                if ( presetTable.findPreset( pText ) == nullptr )
                {
                    outReason = string( "unknown network preset '" ) + pText + "' (" + presetTable.makePresetNames() + ")";
                    return false;
                }
                _testOptions._load._network = pText;
            }
            else
            {
                outReason = "unknown attribute '" + string( name ) + "'";
                return false;
            }
        }
        if ( _testOptions._load._durationSeconds <= 0.0f )
        {
            outReason = "needs durationSeconds > 0 (the measurement window)";
            return false;
        }
        return true;
    }
} // namespace sw
