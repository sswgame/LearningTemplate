#include "pch.h"

#include "Engine/Automation/AutomationScenario.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"

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

    AutomationScenario::AutomationScenario()
        : _listStep{}
        , _name{}
        , _sourcePath{}
        , _fixedDelta{ AutomationScenarioInternal::kDefaultFixedDelta }
        , _timeoutFrames{ AutomationScenarioInternal::kDefaultTimeoutFrames }
        , _startTimeoutFrames{ AutomationScenarioInternal::kDefaultStartTimeoutFrames }
        , _startCondition{ AutomationStartCondition::ScenePlaying }
        , _inputMode{ VirtualInputMode::Exclusive }
        , _bIsolated{ false }
    {
    }

    bool AutomationScenario::loadFromPath( string_view path, string& outError )
    {
        XMLDocument doc;
        if ( doc.loadPath( path ) == false )
        {
            outError = string( path ) + ": " + ( doc.getLastError().empty() ? string( "could not read the scenario file" ) : doc.getLastError() );
            return false;
        }
        return parse( doc.saveToString(), path, outError );
    }

    bool AutomationScenario::parse( string_view xmlText, string_view sourceName, string& outError )
    {
        using Internal = AutomationScenarioInternal;
        *this          = AutomationScenario{};
        _sourcePath    = sourceName.empty() ? string( "<memory>" ) : string( sourceName );

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
            const string_view name  = attribute.getName();
            const utf8*       pText = attribute.getValue();
            bool              bOk   = true;
            if ( name == "name" )
            {
                _name = pText;
            }
            else if ( name == "fixedDelta" )
            {
                bOk = StringUtil::parseFloat( string_view{ pText }, _fixedDelta ) && _fixedDelta > 0.0f;
                if ( bOk == false )
                    reason = string( "fixedDelta must be a positive number, got '" ) + pText + "'";
            }
            else if ( name == "timeoutFrames" )
            {
                bOk = Internal::readUint( pText, "timeoutFrames", _timeoutFrames, reason );
            }
            else if ( name == "startTimeoutFrames" )
            {
                bOk = Internal::readUint( pText, "startTimeoutFrames", _startTimeoutFrames, reason );
            }
            else if ( name == "startAfter" )
            {
                const string_view value = pText;
                if ( value == "ScenePlaying" )
                {
                    _startCondition = AutomationStartCondition::ScenePlaying;
                }
                else if ( value == "Immediately" )
                {
                    _startCondition = AutomationStartCondition::Immediately;
                }
                else
                {
                    bOk    = false;
                    reason = string( "unknown startAfter '" ) + pText + "' (ScenePlaying | Immediately)";
                }
            }
            else if ( name == "isolated" )
            {
                const string_view value = pText;
                bOk                     = value == "true" || value == "false";
                _bIsolated              = value == "true";
                if ( bOk == false )
                    reason = string( "isolated must be true or false, got '" ) + pText + "'";
            }
            else if ( name == "input" )
            {
                const string_view value = pText;
                if ( value == "exclusive" )
                {
                    _inputMode = VirtualInputMode::Exclusive;
                }
                else if ( value == "mixed" )
                {
                    _inputMode = VirtualInputMode::Mixed;
                }
                else
                {
                    bOk    = false;
                    reason = string( "unknown input mode '" ) + pText + "' (exclusive | mixed)";
                }
            }
            else
            {
                bOk    = false;
                reason = "unknown <Scenario> attribute '" + string( name ) + "'";
            }
            if ( bOk == false )
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

        uint32 orderInFile   = 0;
        uint32 previousFrame = 0;
        for ( XMLNode atNode = root.findChild(); atNode.isValid(); atNode = atNode.findNextSibling() )
        {
            if ( string_view{ atNode.getName() } != "At" )
            {
                outError = _sourcePath + ": only <At frame=\"N\"> may sit under <Scenario>, got <" + atNode.getName() + ">";
                return false;
            }
            uint32 frameIndex = 0;
            if ( Internal::readAtFrame( atNode, _fixedDelta, previousFrame, frameIndex, reason ) == false )
            {
                outError = _sourcePath + ": " + reason;
                return false;
            }
            previousFrame          = frameIndex;
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
                step._orderInFile = orderInFile++;
                for ( XMLAttribute attribute = stepNode.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    AutomationAttribute value{};
                    value._name  = attribute.getName();
                    value._value = attribute.getValue();
                    step._listAttribute.push_back( std::move( value ) );
                }
                _listStep.push_back( std::move( step ) );
            }
        }
        // 프레임 순이다(같은 프레임은 파일 순이며 안정 정렬이다).
        std::stable_sort( _listStep.begin(), _listStep.end(), []( const AutomationStep& lhs, const AutomationStep& rhs )
        {
            return lhs._frameIndex < rhs._frameIndex;
        } );
        return true;
    }
} // namespace sw
