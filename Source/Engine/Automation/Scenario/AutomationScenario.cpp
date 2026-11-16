#include "pch.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        struct AutomationScenarioInternal
        {
            static constexpr uint32 kDefaultTimeoutFrames      = 3600;
            static constexpr uint32 kDefaultStartTimeoutFrames = 1200;

            /**
             * @brief 파일을 찾지 못한 이유를 막힌 단계(`[read]`)와 함께 적습니다. 리소스 ID 가 안 풀리는 원인(리소스 루트 · 작업 폴더 · 낱개 파일 금지 구성)이 로그 한 줄에 보이게 한다.
             */
            [[nodiscard]] static string describeReadFailure( string_view path )
            {
                const optional<AbsolutePath> looseFile = engine::getSubsystem<AssetPaths>().findLooseFile( path );
                const string                 loosePath = looseFile.has_value() ? looseFile->str() : string{};
                const bool                   bPackOnly = engine::areEngineServicesBound() && engine::getSubsystem<AssetManager>().getPackManager().isAllowLooseFiles() == false;
                return "[read] not found (content root '" + engine::getSubsystem<AssetPaths>().getRootFolderPath() + "', working folder '" + FileUtil::getCurrentPath() + "', " +
                       ( loosePath.empty() ? string( "loose file unresolved" ) : "loose file '" + loosePath + "' is not a file" ) +
                       ( bPackOnly ? ", this build reads packs only and the id is in none of them)" : ", the id is in none of the mounted packs)" );
            }

            /** @brief 속성 값이 틀렸다는 오류입니다. 대상은 `이름='값'` 입니다. */
            [[nodiscard]] static Error makeValueError( const utf8* pMessage, string_view name, string_view value )
            {
                return Error{ ErrorKind::InvalidArgument, pMessage, name }.appendDetail( "='" ).appendDetail( value ).appendDetail( "'" );
            }

            /** @brief 음이 아닌 정수 속성을 읽습니다. 형식이 틀리면 그 이유입니다. */
            [[nodiscard]] static expected<uint32> readUint( const utf8* pValue, const utf8* pName )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( string_view{ pValue }, value ) == false || value < 0 )
                    return unexpected( makeValueError( "must be a non-negative integer", pName, pValue ) );
                return static_cast<uint32>( value );
            }

            /** @brief 불리언 속성을 읽습니다(철자 표는 `StringUtil::tryParseBool`). 읽지 못하면 그 이유입니다. */
            [[nodiscard]] static expected<bool> readBool( const utf8* pName, const utf8* pValue )
            {
                bool value = false;
                if ( StringUtil::tryParseBool( string_view{ pValue }, value ) == false )
                    return unexpected( makeValueError( "must be a boolean (true or false)", pName, pValue ) );
                return value;
            }

            /** @brief 음이 아닌 초를 프레임으로 바꿉니다(고정 프레임 시간으로 반올림). 형식이 틀리면 그 이유입니다. */
            [[nodiscard]] static expected<uint32> readSeconds( const utf8* pValue, const utf8* pName, float32 fixedDelta )
            {
                float32 seconds = 0.0f;
                if ( StringUtil::parseFloat( string_view{ pValue }, seconds ) == false || seconds < 0.0f )
                    return unexpected( makeValueError( "must be a non-negative number of seconds", pName, pValue ) );
                return static_cast<uint32>( seconds / fixedDelta + 0.5f );
            }

            /**
             * @brief `<Wait frames="N"/>` · `<Wait seconds="S"/>` 의 프레임 수입니다(초는 고정 프레임 시간으로 바꾼다). 둘 가운데 정확히 하나여야 합니다.
             * @details 시간은 이것만 흘린다 — 단계는 적은 순서대로 지금 프레임에 돈다. 조건을 기다리는 것은 `<WaitUntil>` 이다(그동안 시계가 서 있다).
             */
            [[nodiscard]] static expected<uint32> readWait( const XMLNode& waitNode, float32 fixedDelta )
            {
                uint32 timingCount = 0;
                uint32 frames      = 0;
                for ( XMLAttribute attribute = waitNode.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const string_view name  = attribute.getName();
                    const utf8*       pText = attribute.getValue();
                    if ( name != "frames" && name != "seconds" )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "unknown <Wait> attribute (frames · seconds)", name } );
                    const expected<uint32> read = name == "frames" ? readUint( pText, "frames" ) : readSeconds( pText, "seconds", fixedDelta );
                    if ( read.has_value() == false )
                        return read;
                    frames = *read;
                    ++timingCount;
                }
                if ( timingCount != 1 || frames == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "<Wait> needs exactly one of frames=\"N\" · seconds=\"S\" and more than zero frames" } );
                return frames;
            }

            /**
             * @brief `<Launch variable="gv_…" value="…"/>` 하나를 읽어 @p inoutList 에 더합니다.
             * @details 값은 명령줄 토큰 하나가 되므로 공백 · 따옴표를 받지 않는다. 같은 변수를 두 번 적으면 어느 쪽이 이기는지 모호하므로 오류다.
             */
            [[nodiscard]] static expected<void> readLaunchVariable( const XMLNode& node, vector<AutomationAttribute>& inoutList )
            {
                AutomationAttribute entry{};
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const string_view name = attribute.getName();
                    if ( name == "variable" )
                        entry._name = attribute.getValue();
                    else if ( name == "value" )
                        entry._value = attribute.getValue();
                    else
                        return unexpected( Error{ ErrorKind::InvalidArgument, "<Launch> has an unknown attribute (variable · value)", name } );
                }
                const bool bWellFormed = StringUtil::startsWith( entry._name, "gv_" ) && entry._value.empty() == false && entry._value.find_first_of( " 	\"'" ) == string::npos;
                if ( bWellFormed == false )
                    return unexpected( makeValueError( "<Launch> needs variable=\"gv_…\" and a value without spaces or quotes", entry._name, entry._value ) );
                for ( const AutomationAttribute& declared : inoutList )
                {
                    if ( declared._name == entry._name )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "<Launch> declares the variable twice", entry._name } );
                }
                inoutList.push_back( std::move( entry ) );
                return {};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string makeAutomationOutputFolderPath()
    {
        return FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Automation" ); // `kAutomationOutputFolder` 의 디스크 자리 - 작업 폴더가 아니라 실행 산출물 루트다
    }

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
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "frame %# <%#> #%#", _frameIndex, _kind, _orderInFile + 1 );
        return string( resultBuilder.view() );
    }

    string AutomationStep::describeError( const Error& error ) const
    {
        return describe() + ": " + describeReason( error );
    }

    string AutomationStep::describeReason( const Error& error )
    {
        StringBuilder<constant::buffer::kMax512> textBuilder;
        textBuilder.append( error.message() );
        if ( error.detail().empty() == false )
            textBuilder.appendFormat( ": %#", error.detail() );
        return string( textBuilder.view() );
    }

    expected<void> AutomationStep::readPropertiesOf( void* pParams, const TypeInfo& typeInfo, std::initializer_list<string_view> listHandRead ) const
    {
        // 속성은 파일을 읽을 때 글로 풀어 뒀다 — 원소 하나를 다시 지어 XML 칸 읽기(`XMLFields`)와 같은 규칙으로 읽는다.
        XMLDocument   document;
        const XMLNode node = document.appendRoot( _kind.c_str() );
        for ( const AutomationAttribute& attribute : _listAttribute )
        {
            node.appendAttribute( attribute._name.c_str(), string_view{ attribute._value } );
        }
        // 칸 읽기의 글은 앞머리(출처 이름)를 붙인다 — 단계 위치는 `describeError` 가 붙이므로 출처는 짧은 이름만 준다.
        string             firstIssue;
        XMLFieldsIssueSink sink;
        sink._pContext = &firstIssue;
        sink._pfnAdd   = []( void* pContext, const utf8* /*pID*/, string_view text )
        {
            string& issue = *static_cast<string*>( pContext );
            if ( issue.empty() ) // 첫 오류만 — 뒤 오류는 대개 첫 오류의 결과다
                issue = string( text );
        };
        if ( XMLFields::readProperties( node, pParams, typeInfo, "attributes", listHandRead, nullptr, sink ) )
            return {};
        return unexpected( Error{ ErrorKind::InvalidArgument, "attributes do not fit the step", firstIssue } );
    }

    expected<void> AutomationStep::hasOnlyAttributes( std::initializer_list<string_view> listAllowed ) const
    {
        for ( const AutomationAttribute& attribute : _listAttribute )
        {
            bool bKnown = false;
            for ( const string_view allowed : listAllowed )
            {
                bKnown = bKnown || string_view{ attribute._name } == allowed;
            }
            if ( bKnown == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown attribute", attribute._name } );
        }
        return {};
    }

    expected<void> AutomationStep::hasRequiredAttributes( std::initializer_list<string_view> listAllowed, std::initializer_list<string_view> listRequired ) const
    {
        const expected<void> allowed = hasOnlyAttributes( listAllowed );
        if ( allowed.has_value() == false )
            return allowed;
        for ( const string_view required : listRequired )
        {
            if ( findAttribute( required ) == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "needs the attribute", required } );
        }
        return {};
    }

    expected<const string*> AutomationStep::requireAttribute( string_view name ) const
    {
        const string* pValue = findAttribute( name );
        if ( pValue == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "needs the attribute", name } );
        return pValue;
    }

    AutomationScenario::AutomationScenario()
        : _listStep{}
        , _testOptions{}
        , _name{}
        , _sourcePath{}
        , _fixedDelta{ constant::time::kDefaultFixedStepSeconds }
        , _wallTimeoutSeconds{ kDefaultWallTimeoutSeconds }
        , _timeoutFrames{ AutomationScenarioInternal::kDefaultTimeoutFrames }
        , _startTimeoutFrames{ AutomationScenarioInternal::kDefaultStartTimeoutFrames }
        , _startCondition{ AutomationStartCondition::ScenePlaying }
        , _inputMode{ VirtualInputMode::Exclusive }
    {
    }

    const AutomationStep* AutomationScenario::findOtherStepAtSameFrame( const AutomationStep& step ) const
    {
        for ( const AutomationStep& otherStep : _listStep )
        {
            if ( &otherStep != &step && otherStep._frameIndex == step._frameIndex )
                return &otherStep;
        }
        return nullptr;
    }

    void AutomationScenario::bindScratchInstances()
    {
        for ( AutomationScratchFolder& folder : _testOptions._listScratchFolder )
        {
            const string instanceID = AutomationScratch::makeInstanceID( folder._scratchID );
            for ( AutomationStep& step : _listStep )
            {
                for ( AutomationAttribute& attribute : step._listAttribute )
                {
                    attribute._value = AutomationScratch::rebaseText( attribute._value, folder._scratchID, instanceID );
                }
            }
            folder._declaredID = folder._scratchID;
            folder._scratchID  = instanceID;
        }
    }

    void AutomationScenario::bindOutputDirectory( string_view outputDirectory )
    {
        for ( AutomationStep& step : _listStep )
        {
            for ( AutomationAttribute& attribute : step._listAttribute )
                attribute._value = StringUtil::replace( attribute._value, kAutomationOutputToken, outputDirectory );
        }
    }

    expected<void> AutomationScenario::loadFromPath( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        XMLDocument       doc;
        if ( doc.loadResource( assetID ) == false )
        {
            // 단계를 가른다 — 읽기(`[read]`, 리소스 ID 가 안 풀린다)와 XML 문법(`[xml]`)은 고치는 곳이 다르다. 형식 검사(`parse`)의 오류는 그 문장이 말한다.
            if ( AssetIO::hasAsset( assetID ) )
                return unexpected( Error{ ErrorKind::InvalidArgument, "[xml] the scenario is not well-formed XML", doc.getLastError() } );
            return unexpected( Error{ ErrorKind::NotFound, "[read] the scenario file was not found", AutomationScenarioInternal::describeReadFailure( path ) } );
        }
        return parse( doc.saveToString(), path );
    }

    expected<void> AutomationScenario::parseRootAttribute( string_view name, const utf8* pText )
    {
        using Internal = AutomationScenarioInternal;
        if ( name == "name" )
        {
            _name = pText;
            return {};
        }
        if ( name == "fixedDelta" )
        {
            const bool bOk = StringUtil::parseFloat( string_view{ pText }, _fixedDelta ) && _fixedDelta > 0.0f;
            if ( bOk == false )
                return unexpected( Internal::makeValueError( "must be a positive number", name, pText ) );
            return {};
        }
        if ( name == "timeoutFrames" || name == "startTimeoutFrames" )
        {
            uint32&                target = name == "timeoutFrames" ? _timeoutFrames : _startTimeoutFrames;
            const expected<uint32> read   = Internal::readUint( pText, name == "timeoutFrames" ? "timeoutFrames" : "startTimeoutFrames" );
            if ( read.has_value() == false )
                return unexpected( read.error() );
            target = *read;
            return {};
        }
        if ( name == "wallTimeoutSeconds" )
        {
            // 0 이나 음수로 상한을 끄지 못한다 — 끝나지 않는 시나리오가 테스트의 프로세스 시한(보고 없는 kill)까지 가는 것이 이 값이 막는 결함이다.
            // 상한은 시계 보류 시한(`AutomationTimeLimits`)보다 짧아야 한다 — 그 이상이면 프로세스 시한이 먼저 죽여 보고가 없다. 모든 시나리오 파일을 읽는 데이터 테스트가 이 자리에서 잡는다.
            const bool bParsed  = StringUtil::parseFloat( string_view{ pText }, _wallTimeoutSeconds );
            const bool bInRange = bParsed && _wallTimeoutSeconds > 0.0f && static_cast<float64>( _wallTimeoutSeconds ) < AutomationTimeLimits::kClockHoldTimeoutSeconds;
            if ( bInRange == false )
                return unexpected( Internal::makeValueError( "must be a positive number of seconds below the clock-hold timeout (AutomationTimeLimits::kClockHoldTimeoutSeconds)", name, pText ) );
            return {};
        }
        if ( name == "startAfter" )
        {
            const string_view value = pText;
            if ( value == "ScenePlaying" )
                _startCondition = AutomationStartCondition::ScenePlaying;
            else if ( value == "Immediately" )
                _startCondition = AutomationStartCondition::Immediately;
            else
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown startAfter (ScenePlaying | Immediately)", value } );
            return {};
        }
        if ( name == "requireForeground" || name == "editor" || name == "present" )
        {
            bool&                target = name == "requireForeground" ? _testOptions._bRequiresForeground : name == "editor" ? _testOptions._bRequiresEditor
                                                                                                                             : _testOptions._bPresents;
            const expected<bool> read   = Internal::readBool( name == "requireForeground" ? "requireForeground" : name == "editor" ? "editor"
                                                                                                                                   : "present",
                                                            pText );
            if ( read.has_value() == false )
                return unexpected( read.error() );
            target = *read;
            return {};
        }
        if ( name == "requiresModule" )
        {
            _testOptions._listRequiredModule.clear();
            for ( const string_view moduleName : StringUtil::split( pText, ",", StringSplitOptions{ true, true } ) )
            {
                _testOptions._listRequiredModule.emplace_back( moduleName );
            }
            if ( _testOptions._listRequiredModule.empty() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "requiresModule needs at least one module name (comma separated)" } );
            return {};
        }
        if ( name == "input" )
        {
            const string_view value = pText;
            if ( value == "exclusive" )
                _inputMode = VirtualInputMode::Exclusive;
            else if ( value == "mixed" )
                _inputMode = VirtualInputMode::Mixed;
            else
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown input mode (exclusive | mixed)", value } );
            return {};
        }
        return unexpected( Error{ ErrorKind::InvalidArgument, "unknown <Scenario> attribute", name } );
    }

    expected<void> AutomationScenario::parseStepNode( const XMLNode& stepNode, uint32 frameIndex, uint32& inoutOrderInFile )
    {
        const string_view kind = stepNode.getName();
        if ( kind.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scenario> holds text - only step elements may sit there", _sourcePath } );
        if ( stepNode.findChild().isValid() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a step has child elements - steps are flat, written in the order they run", kind } );
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
        return {};
    }

    expected<void> AutomationScenario::parseChildren( const XMLNode& root )
    {
        using Internal          = AutomationScenarioInternal;
        uint32 orderInFile      = 0;
        uint32 frameCursor      = 0;     ///< 지금까지 `<Wait>` 가 흘린 프레임 — 단계는 적은 순서대로 이 프레임에 돈다
        bool   bCursorFrameUsed = false; ///< 지금 프레임에 이미 단계가 있다
        for ( XMLNode atNode = root.findChild(); atNode.isValid(); atNode = atNode.findNextSibling() )
        {
            const string_view elementName = atNode.getName();
            if ( elementName == "Load" )
            {
                if ( _testOptions._load._bDeclared )
                    return unexpected( Error{ ErrorKind::AlreadyExists, "only one <Load> may sit under <Scenario>", _sourcePath } );
                if ( expected<void> loaded = parseLoad( atNode ); loaded.has_value() == false )
                    return loaded;
                continue;
            }
            if ( elementName == "Scratch" )
            {
                // 칸은 리플렉션 구조체가 정한다 — 원본 폴더는 경로 칸이라 참조 검사 · 옮기기가 따라간다(`AssetPath`).
                AutomationScratchParams params;
                if ( XMLFields::readProperties( atNode, params, _sourcePath ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch> needs from=\"<source folder>\" and as=\"scratch/<folder>\"", _sourcePath } );
                AutomationScratchFolder folder;
                folder._sourceID  = std::move( params._from );
                folder._scratchID = std::move( params._as );
                if ( expected<void> valid = AutomationScratch::isValidFolder( folder ); valid.has_value() == false )
                    return valid;
                _testOptions._listScratchFolder.push_back( std::move( folder ) );
                continue;
            }
            if ( elementName == "AllowLog" )
            {
                // 허용은 시나리오 안에 사유와 함께 둔다(예전의 전역 허용 목록 대신). 와일드카드 · 빈 사유는 받지 않는다.
                const string_view id     = atNode.getAttributeText( "id" );
                const string_view reason = atNode.getAttributeText( "reason" );
                const bool        bValid = id.empty() == false && reason.empty() == false && id.find_first_of( "*?" ) == string_view::npos &&
                                    atNode.getFirstAttribute().isValid() && atNode.getFirstAttribute().getNext().isValid() &&
                                    atNode.getFirstAttribute().getNext().getNext().isValid() == false;
                if ( bValid == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "<AllowLog> needs exactly id=\"<log id>\" and reason=\"<why it is allowed>\" (no wildcard)", id } );
                _testOptions._listAllowedLog.push_back( AutomationAttribute{ string( id ), string( reason ) } );
                continue;
            }
            if ( elementName == "Launch" )
            {
                if ( expected<void> launch = Internal::readLaunchVariable( atNode, _testOptions._listLaunchVariable ); launch.has_value() == false )
                    return launch;
                continue;
            }
            if ( elementName == "At" )
                return unexpected( Error{ ErrorKind::Unsupported,
                                          "<At frame> is gone - write the steps in the order they run and put <Wait frames=\"N\"/> where time must pass (<WaitUntil> waits for a condition)",
                                          _sourcePath } );
            if ( elementName == "Wait" )
            {
                const expected<uint32> frames = Internal::readWait( atNode, _fixedDelta );
                if ( frames.has_value() == false )
                    return unexpected( frames.error() );
                frameCursor += *frames;
                bCursorFrameUsed = false;
                continue;
            }
            // 기다리기만 하는 단계(`<WaitUntil>` · `<EditorWaitStable>`)는 조건이 설 때까지 시계를 붙잡는다 — 붙잡힌 프레임의 다른 단계(입력 · 입력 전 단계)가 다시 돌지
            // 않게 제 프레임을 혼자 갖는다. 앞 단계가 이 프레임에 있으면 다음 프레임으로 가고, 뒤 단계는 그다음 프레임에서 돈다(적은 순서는 그대로다 — 쓰는 사람이 프레임을 셀
            // 일이 없다). 등록 모양으로 가린다(`isWaitOnly`) — 단계를 등록한 모듈이 없는 프로세스에서는 모르는 단계라 시작 검사가 먼저 막는다.
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( elementName );
            const bool                        bWaitUntil    = pRegistration != nullptr && pRegistration->isWaitOnly();
            if ( bWaitUntil && bCursorFrameUsed )
                ++frameCursor;
            if ( expected<void> parsed = parseStepNode( atNode, frameCursor, orderInFile ); parsed.has_value() == false )
                return parsed;
            bCursorFrameUsed = true;
            if ( bWaitUntil )
            {
                ++frameCursor;
                bCursorFrameUsed = false;
            }
        }
        return {};
    }

    expected<void> AutomationScenario::parse( string_view xmlText, string_view sourceName )
    {
        *this       = AutomationScenario{};
        _sourcePath = sourceName.empty() ? string( "<memory>" ) : string( sourceName );

        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "[xml] the scenario is not well-formed XML", doc.getLastError() } );
        const XMLNode root = doc.getRoot( nullptr );
        if ( root.isValid() == false || string_view{ root.getName() } != "Scenario" )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the root element must be <Scenario>", _sourcePath } );

        // 루트 속성: 모르는 속성은 읽기 오류다(오타가 기본값으로 조용히 바뀌지 않게 한다).
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( expected<void> read = parseRootAttribute( attribute.getName(), attribute.getValue() ); read.has_value() == false )
                return read;
        }
        if ( _name.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scenario> needs a name", _sourcePath } );

        if ( expected<void> children = parseChildren( root ); children.has_value() == false )
            return children;
        // 프레임 순이다(같은 프레임은 파일 순이며 안정 정렬이다).
        VectorUtil::stableSort( _listStep, []( const AutomationStep& lhs, const AutomationStep& rhs )
        {
            return lhs._frameIndex < rhs._frameIndex;
        } );
        // 부하 시나리오는 측정 창이 끝낸다 — `<Pass/>` 가 있으면 창이 닫히기 전에 프로세스가 끝나 보고가 없다.
        const string outputFolderText = string( kAutomationOutputFolder ) + "/"; // 시나리오 글 안의 직접 표기를 찾는다(디스크 경로가 아니다)
        for ( const AutomationStep& step : _listStep )
        {
            for ( const AutomationAttribute& attribute : step._listAttribute )
            {
                if ( attribute._value.find( outputFolderText ) != string::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument,
                                              "write the run's output folder as {out} (kAutomationOutputToken) - a literal path has no process suffix, so runs side by side (one per backend) "
                                              "overwrite each other's files",
                                              step.describe() } );
            }
            if ( _testOptions._load._bDeclared && step._kind == "Pass" )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a scenario with <Load> ends when its measurement window closes - remove <Pass/>", step.describe() } );
        }
        // 프레임 시한은 적은 프레임(고정 대기 · 단계)만 센다 — 붙잡는 대기는 저마다 제 시한이 있다. 그래서 마지막 단계가 시한 밖이면 실행 전에 늘 지는 파일이다.
        const bool bLastStepPastTimeout = _testOptions._load._bDeclared == false && _listStep.empty() == false && _listStep.back()._frameIndex > _timeoutFrames;
        if ( bLastStepPastTimeout )
        {
            utf8 arrFrames[constant::buffer::kMax64]{};
            formatstring( arrFrames, constant::buffer::kMax64, " at frame %#, timeoutFrames %#", _listStep.back()._frameIndex, _timeoutFrames );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the last step is past timeoutFrames (the limit counts written frames - fixed waits and steps; each held wait keeps its own limit)",
                                      _listStep.back().describe() }
                                   .appendDetail( arrFrames ) );
        }
        return {};
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

    expected<void> AutomationScenario::parseLoad( const XMLNode& node )
    {
        using Internal                = AutomationScenarioInternal;
        _testOptions._load._bDeclared = true;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const string_view name  = attribute.getName();
            const utf8*       pText = attribute.getValue();
            if ( name == "clients" )
            {
                const expected<uint32> read = Internal::readUint( pText, "clients" );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                _testOptions._load._clientCount = *read;
            }
            else if ( name == "meshes" )
            {
                const expected<uint32> read = Internal::readUint( pText, "meshes" );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                _testOptions._load._meshCount = *read;
            }
            else if ( name == "movers" )
            {
                const expected<uint32> read = Internal::readUint( pText, "movers" );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                _testOptions._load._moverCount = *read;
            }
            else if ( name == "durationSeconds" || name == "rampUpSeconds" )
            {
                float32& outSeconds = name == "durationSeconds" ? _testOptions._load._durationSeconds : _testOptions._load._rampUpSeconds;
                if ( StringUtil::parseFloat( string_view{ pText }, outSeconds ) == false || outSeconds < 0.0f )
                    return unexpected( Internal::makeValueError( "<Load> needs a non-negative number", name, pText ) );
            }
            else if ( name == "network" )
            {
                NetEmulationPresetTable presetTable;
                const DataPath&         presetPath = engine::getSubsystem<EngineDefaultAssets>()._netEmulationPresets;
                if ( presetTable.loadFromAsset( AssetID{ presetPath.get() } ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "<Load network> presets could not be read", presetPath.get() } );
                if ( presetTable.findPreset( pText ) == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "<Load> names an unknown network preset", pText }.appendDetail( " (" ).appendDetail( presetTable.makePresetNames() ).appendDetail( ")" ) );
                _testOptions._load._network = pText;
            }
            else
                return unexpected( Error{ ErrorKind::InvalidArgument, "<Load> has an unknown attribute", name } );
        }
        if ( _testOptions._load._durationSeconds <= 0.0f )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Load> needs durationSeconds > 0 (the measurement window)" } );
        return {};
    }
} // namespace sw
