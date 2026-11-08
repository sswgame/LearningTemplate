#include "pch.h"

#include "Engine/Graphics/Material/Dev/MaterialGraphCompileResult.h"

#include "Core/Container/DependencyGraph.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/fixed_string.h"

#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include <optional>

namespace sw
{
    namespace
    {
        struct MaterialGraphCompilerInternal
        {
            /** @brief 칸 이름 · HLSL 형식이 든 값 하나입니다. 형식이 `Invalid` 면 이미 문제를 남긴 값이다 — 뒤 노드가 같은 문제를 다시 알리지 않는다. */
            struct Value
            {
                string                 _expression;
                MaterialGraphValueType _type{ MaterialGraphValueType::Invalid };
            };

            struct NamedValue
            {
                string _name;
                Value  _value;
            };

            /** @brief 노드 하나가 낸 출력들입니다. */
            struct NodeResult
            {
                vector<NamedValue> _listOutput;
            };

            [[nodiscard]] static bool isIdentifierChar( utf8 c )
            {
                const bool bLetter = ( 'a' <= c && c <= 'z' ) || ( 'A' <= c && c <= 'Z' );
                return bLetter || ( '0' <= c && c <= '9' ) || c == '_';
            }

            [[nodiscard]] static bool isIdentifier( string_view text )
            {
                if ( text.empty() || ( '0' <= text.front() && text.front() <= '9' ) )
                    return false;
                for ( const utf8 c : text )
                {
                    if ( isIdentifierChar( c ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 노드 이름은 생성물의 주석에 들어간다 — 줄을 끊는 글자를 받지 않는다. */
            [[nodiscard]] static bool isNodeName( string_view text )
            {
                if ( text.empty() )
                    return false;
                for ( const utf8 c : text )
                {
                    if ( isIdentifierChar( c ) == false && c != ' ' && c != '-' && c != '.' )
                        return false;
                }
                return true;
            }

            /** @brief 경로는 생성된 머티리얼의 XML 속성에 그대로 들어간다. */
            [[nodiscard]] static bool isAttributeSafe( string_view text )
            {
                return text.find_first_of( "\"<>&\n\r" ) == string_view::npos;
            }

            [[nodiscard]] static string formatFloatLiteral( float32 value )
            {
                fixed_string<constant::buffer::kMax64> buffer;
                formatstring( buffer.data(), buffer.capacity(), "%g", value );
                string text{ buffer.c_str() };
                if ( text.find_first_of( ".en" ) == string::npos )
                    text.append( ".0" );
                text.append( "f" );
                return text;
            }

            [[nodiscard]] static string formatPlainFloat( float32 value )
            {
                fixed_string<constant::buffer::kMax64> buffer;
                formatstring( buffer.data(), buffer.capacity(), "%g", value );
                return string{ buffer.c_str() };
            }

            /** @brief @p value 를 @p type 으로 편 식입니다. float 만 넓어지고, 다른 벡터 크기는 false 입니다. */
            [[nodiscard]] static bool coerce( const Value& value, MaterialGraphValueType type, string& outExpression )
            {
                if ( value._type == type )
                {
                    outExpression = value._expression;
                    return true;
                }
                if ( value._type != MaterialGraphValueType::Float )
                    return false;
                const uint32 count = static_cast<uint32>( type );
                outExpression      = MaterialGraphNodeRegistry::getHLSLTypeName( type );
                outExpression.append( "( " );
                for ( uint32 index = 0; index < count; ++index )
                {
                    outExpression.append( index == 0 ? "" : ", " );
                    outExpression.append( value._expression );
                }
                outExpression.append( " )" );
                return true;
            }

            /**
             * @brief 수치 글(공백 · 쉼표로 나눈다)을 HLSL 상수로 읽습니다. @p type 이 `Any` 면 수의 개수가 형식이고, 벡터인데 수가 하나면 모든 칸에 씁니다.
             * @return 수가 아니거나 개수가 맞지 않으면 false 입니다.
             */
            [[nodiscard]] static bool makeLiteral( string_view text, MaterialGraphValueType type, Value& outValue )
            {
                float32 arrValue[4]{};
                uint32  count = 0;
                for ( const string_view token : StringUtil::split( text, " ," ) )
                {
                    if ( token.empty() )
                        continue;
                    if ( count >= 4 || StringUtil::parseFloat( token, arrValue[count] ) == false )
                        return false;
                    ++count;
                }
                if ( count == 0 )
                    return false;
                const MaterialGraphValueType resolved = type == MaterialGraphValueType::Any ? static_cast<MaterialGraphValueType>( count ) : type;
                const uint32                 wanted   = static_cast<uint32>( resolved );
                if ( wanted > 4 || ( count != 1 && count != wanted ) )
                    return false;
                outValue._type = resolved;
                if ( resolved == MaterialGraphValueType::Float )
                {
                    outValue._expression = formatFloatLiteral( arrValue[0] );
                    return true;
                }
                outValue._expression = MaterialGraphNodeRegistry::getHLSLTypeName( resolved );
                outValue._expression.append( "( " );
                for ( uint32 index = 0; index < wanted; ++index )
                {
                    outValue._expression.append( index == 0 ? "" : ", " );
                    outValue._expression.append( formatFloatLiteral( arrValue[count == 1 ? 0 : index] ) );
                }
                outValue._expression.append( " )" );
                return true;
            }

            /** @brief 등록의 기본값 글입니다 — 수로 시작하면 상수이고, 아니면 선언한 형식의 HLSL 식입니다. */
            [[nodiscard]] static bool makeDefault( string_view text, MaterialGraphValueType type, Value& outValue )
            {
                const utf8 first    = text.empty() ? '\0' : text.front();
                const bool bNumeric = ( '0' <= first && first <= '9' ) || first == '-' || first == '.';
                if ( bNumeric )
                    return makeLiteral( text, type, outValue );
                if ( text.empty() || type == MaterialGraphValueType::Any )
                    return false;
                outValue._expression = string{ text };
                outValue._type       = type;
                return true;
            }

            /** @brief 층이 얹을 수 있는 표면 칸 하나입니다(층 그래프의 `GraphOutput` · `GraphInput` 이름). */
            struct LayerAttribute
            {
                string_view            _pin;   ///< 층 그래프가 쓰는 이름(`Output` 노드의 핀과 같다)
                string_view            _field; ///< `MaterialGraphOutput` 의 칸
                MaterialGraphValueType _type;
            };

            /**
             * @brief 층이 얹는 칸입니다. 알파는 넣지 않는다 — 알파 방식 · 컷오프는 머티리얼의 렌더 상태라 층이 바꾸면 패스가 달라진다.
             *        언리얼 Material Layers 는 Material Attributes 를 통째로 섞는다(기억에 의존). 여기서는 층이 내지 않은 칸은 아래 값 그대로다(젖음은 거칠기 · 색만 바꾼다).
             */
            static constexpr LayerAttribute kArrLayerAttribute[] = {
                {"BaseColor", "_baseColor", MaterialGraphValueType::Float3},
                { "Metallic",  "_metallic",  MaterialGraphValueType::Float},
                {"Roughness", "_roughness",  MaterialGraphValueType::Float},
                {   "Normal",    "_normal", MaterialGraphValueType::Float3},
                { "Emissive",  "_emissive", MaterialGraphValueType::Float3},
            };

            /** @brief 층 상한입니다(`Config/Dev/MaterialLayers.json`). 층마다 칸 · 텍스처 표본 · 섞기 줄이 늘어 셰이더 비용의 상한을 데이터로 둔다. */
            struct LayerLimits
            {
                uint32 _maxLayerCount{ 0 };       ///< 머티리얼 하나의 층 수
                uint32 _maxMaskChannelCount{ 0 }; ///< 마스크 텍스처(`_layerMask`)에서 쓸 수 있는 채널 수(R 부터)
            };

            static constexpr const utf8* kLayerLimitsPath = "Config/Dev/MaterialLayers.json";
            static constexpr string_view kLayerMaskField  = "_layerMask";
            static constexpr string_view kMaskChannels    = "RGBA";

            /** @brief 층 상한을 읽습니다. 파일 · 칸이 없거나 0 이면 @p outError 에 까닭을 적고 false 입니다(폴백 없음). */
            [[nodiscard]] static bool loadLayerLimits( LayerLimits& outLimits, string& outError )
            {
                const string path = FileUtil::joinPath( ProcessRoots::get()._project.str(), kLayerLimitsPath );
                string       text;
                JSONDocument doc;
                if ( FileUtil::readTextFile( path, text ) == false || doc.parse( text, kLayerLimitsPath ) == false )
                {
                    outError = string{ kLayerLimitsPath }.append( " cannot be read" );
                    return false;
                }
                const std::optional<uint32> maxLayerCount       = doc.getRoot().get( "_maxLayerCount" ).read<uint32>();
                const std::optional<uint32> maxMaskChannelCount = doc.getRoot().get( "_maxMaskChannelCount" ).read<uint32>();
                const bool                  bValid              = maxLayerCount.has_value() && *maxLayerCount > 0 && maxMaskChannelCount.has_value() && *maxMaskChannelCount > 0 && *maxMaskChannelCount <= kMaskChannels.size();
                if ( bValid == false )
                {
                    outError = string{ kLayerLimitsPath }.append( " needs _maxLayerCount (1 or more) and _maxMaskChannelCount (1 to 4)" );
                    return false;
                }
                outLimits._maxLayerCount       = *maxLayerCount;
                outLimits._maxMaskChannelCount = *maxMaskChannelCount;
                return true;
            }

            /** @brief 셰이딩 모델 Hair 의 머티리얼 칸을 덧붙입니다(이름 · 범위는 hair.material 의 같은 뜻 칸과 같다). */
            static void appendHairFields( vector<MaterialGraphField>& inoutListField )
            {
                static constexpr float32 kHairShiftLimit = 0.2f; ///< 큐티클 기울기 칸의 범위(± 라디안, hair.material `_shift` 와 같다)
                const auto               add             = [&inoutListField]( const utf8* pName, const utf8* pDefault, float32 minValue, float32 maxValue )
                {
                    MaterialGraphField field;
                    field._name         = pName;
                    field._hlslType     = "float";
                    field._propertyType = "Range";
                    field._defaultValue = pDefault;
                    field._min          = minValue;
                    field._max          = maxValue;
                    inoutListField.push_back( std::move( field ) );
                };
                add( "_hairShift", "0.035", -kHairShiftLimit, kHairShiftLimit );
                add( "_hairScatter", "0.5", 0.0f, 1.0f );
                add( "_hairStrandAxis", "1", 0.0f, 1.0f );
            }

            /** @brief 컴파일 한 번의 상태와 단계입니다. */
            struct Session
            {
                vector<MaterialGraphField> _listField;
                vector<MaterialGraphIssue> _listIssue;
                vector<string>             _listGraphStack;  ///< 펼치는 중인 하위 그래프(순환 검사)
                string                     _body;            ///< `evaluateMaterialGraph` 의 본문
                string                     _outputLines;     ///< 출력 칸 대입. 층은 그 뒤에 이어 쓴다(층이 아래 칸 `output._x` 를 읽는다)
                string                     _parameterPrefix; ///< 층 안 파라미터 칸 이름의 머리(`_<층 노드>`) — 같은 층 파일을 두 번 써도 칸이 갈린다
                LayerLimits                _layerLimits;
                uint32                     _nextVariable{ 1 };
                bool                       _bEmissive{ false };         ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
                bool                       _bHair{ false };             ///< NOLINT(sw-bool-flag): 집합체다. 출력의 셰이딩 모델이 Hair 다
                bool                       _bLayerPhase{ false };       ///< NOLINT(sw-bool-flag): 집합체다. 출력 노드를 펼친 뒤다 — 조각을 `_outputLines` 에 쓴다
                bool                       _bLayerMaskSampled{ false }; ///< NOLINT(sw-bool-flag): 집합체다. `_layerMask` 를 이미 읽었다(층마다 다시 읽지 않는다)

                /** @brief 조각 줄을 쓸 곳입니다. 층의 조각은 출력 대입 뒤에 와야 아래 칸을 읽는다. */
                [[nodiscard]] string& emitTarget() { return _bLayerPhase ? _outputLines : _body; }

                void report( string_view scope, string_view nodeName, const utf8* pID, string message )
                {
                    MaterialGraphIssue issue;
                    issue._nodeName = string{ scope };
                    issue._nodeName.append( nodeName );
                    issue._id      = pID;
                    issue._message = std::move( message );
                    _listIssue.push_back( std::move( issue ) );
                }

                /** @brief 종류 · 이름 · 링크 · 값이 이 그래프 안에서 맞는지 봅니다(핀이 파일에서 오는 하위 그래프 노드의 링크는 펼칠 때 본다). */
                [[nodiscard]] bool validateNodes( const MaterialGraphDef& graph, string_view scope )
                {
                    const size_t issueCount = _listIssue.size();
                    for ( size_t nodeIndex = 0; nodeIndex < graph._listNode.size(); ++nodeIndex )
                    {
                        const MaterialGraphNodeDef& node = graph._listNode[nodeIndex];
                        if ( isNodeName( node._name ) == false )
                            report( scope, node._name, "BadName", "node name may hold only letters, digits, space, '_', '-' and '.'" );
                        if ( static_cast<size_t>( graph.findNodeIndex( node._name ) ) != nodeIndex )
                            report( scope, node._name, "DuplicateName", "another node has the same name" );
                        const MaterialGraphNodeKind* pKind = MaterialGraphNodeRegistry::find( node._kind );
                        if ( pKind == nullptr )
                        {
                            string message{ "unknown node kind '" };
                            message.append( node._kind ).append( "'" );
                            report( scope, node._name, "UnknownKind", std::move( message ) );
                            continue;
                        }
                        const bool bSubgraphNode = pKind->getRole() == MaterialGraphNodeRole::Subgraph;
                        for ( const MaterialGraphLink& link : node._listLink )
                        {
                            if ( bSubgraphNode == false && pKind->findInputIndex( link._pin ) < 0 )
                            {
                                string message{ "kind has no input pin '" };
                                message.append( link._pin ).append( "'" );
                                report( scope, node._name, "UnknownPin", std::move( message ) );
                            }
                            if ( graph.findNodeIndex( link._fromNode ) < 0 )
                            {
                                string message{ "input '" };
                                message.append( link._pin ).append( "' reads missing node '" ).append( link._fromNode ).append( "'" );
                                report( scope, node._name, "MissingNode", std::move( message ) );
                            }
                        }
                        for ( const MaterialGraphValue& value : node._listValue )
                        {
                            if ( bSubgraphNode == false && pKind->findInputIndex( value._name ) < 0 && pKind->findSetting( value._name ) == nullptr )
                            {
                                string message{ "kind has no setting or input named '" };
                                message.append( value._name ).append( "'" );
                                report( scope, node._name, "UnknownValue", std::move( message ) );
                            }
                        }
                        if ( isAttributeSafe( node._assetPath ) == false )
                            report( scope, node._name, "BadValue", "asset path holds a character that cannot be written to the material" );
                    }
                    return _listIssue.size() == issueCount;
                }

                /**
                 * @brief 입력 핀 하나의 값입니다: 링크 → 노드 값(상수) → 등록의 기본값. 필수인데 아무것도 없으면 문제를 남기고 `Invalid` 입니다.
                 * @param defaultText 기본값 글. 비면 필수 핀이다
                 */
                [[nodiscard]] Value resolveInput( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, string_view pin, MaterialGraphValueType type, string_view defaultText, string_view scope )
                {
                    Value                    value;
                    const MaterialGraphLink* pLink = node.findLink( pin );
                    if ( pLink != nullptr )
                    {
                        const int32 fromIndex = graph.findNodeIndex( pLink->_fromNode );
                        for ( const NamedValue& output : listResult[static_cast<size_t>( fromIndex )]._listOutput )
                        {
                            if ( output._name == pLink->_fromPin )
                                return output._value;
                        }
                        // 앞 노드가 이미 문제를 남겼으면 출력이 비어 있다 — 없는 핀이라고 또 말하지 않는다.
                        if ( listResult[static_cast<size_t>( fromIndex )]._listOutput.empty() == false )
                        {
                            string message{ "input '" };
                            message.append( pin ).append( "' reads missing output '" ).append( pLink->_fromPin ).append( "' of node '" ).append( pLink->_fromNode ).append( "'" );
                            report( scope, node._name, "UnknownPin", std::move( message ) );
                        }
                        return value;
                    }
                    const MaterialGraphValue* pValue = node.findValue( pin );
                    if ( pValue != nullptr )
                    {
                        if ( makeLiteral( pValue->_value, type, value ) )
                            return value;
                        string message{ "value '" };
                        message.append( pValue->_value ).append( "' of input '" ).append( pin ).append( "' is not " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( type ) );
                        report( scope, node._name, "BadValue", std::move( message ) );
                        return Value{};
                    }
                    if ( defaultText.empty() == false )
                    {
                        if ( makeDefault( defaultText, type, value ) == false )
                            report( scope, node._name, "BadDefault", "the kind's default value cannot be read" );
                        return value;
                    }
                    string message{ "required input pin '" };
                    message.append( pin ).append( "' is not connected" );
                    report( scope, node._name, "MissingPin", std::move( message ) );
                    return value;
                }

                /** @brief `any` 핀들의 형식을 정합니다. 서로 다른 크기의 벡터가 섞이면 문제를 남깁니다. */
                [[nodiscard]] MaterialGraphValueType resolveAny( const MaterialGraphNodeKind& kind, const vector<Value>& listInput, const MaterialGraphNodeDef& node, string_view scope )
                {
                    MaterialGraphValueType resolved = MaterialGraphValueType::Float;
                    for ( uint32 index = 0; index < kind.getInputCount(); ++index )
                    {
                        const MaterialGraphValueType type = listInput[index]._type;
                        if ( kind.getInput( index )._type != MaterialGraphValueType::Any || type == MaterialGraphValueType::Invalid || type == MaterialGraphValueType::Float )
                            continue;
                        if ( resolved != MaterialGraphValueType::Float && resolved != type )
                        {
                            string message{ "pin '" };
                            message.append( kind.getInput( index )._name ).append( "' is " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( type ) ).append( " but another input is " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( resolved ) );
                            report( scope, node._name, "TypeMismatch", std::move( message ) );
                            continue;
                        }
                        resolved = type;
                    }
                    return resolved;
                }

                /** @brief 파라미터 노드의 머티리얼 칸을 올리고 칸 이름을 돌려줍니다. 같은 이름 · 같은 형식은 한 칸을 함께 쓴다. 문제가 있으면 빈 글입니다. */
                [[nodiscard]] string registerParameter( const MaterialGraphNodeKind& kind, const MaterialGraphNodeDef& node, string_view scope )
                {
                    const string localName = readSettingText( kind, node, "Parameter" );
                    if ( isIdentifier( localName ) == false || localName.front() != '_' || MaterialPacking::isPaddingMember( localName ) || localName == "_alphaCutoff" )
                    {
                        string message{ "parameter name '" };
                        message.append( localName ).append( "' must be an identifier starting with '_' and not _alphaCutoff or _swPad*" );
                        report( scope, node._name, "BadParameter", std::move( message ) );
                        return {};
                    }
                    const string name = _parameterPrefix + localName;
                    for ( const MaterialGraphField& field : _listField )
                    {
                        if ( field._name != name )
                            continue;
                        if ( field._hlslType == kind.getParameterHLSLType() && field._propertyType == kind.getParameterPropertyType() )
                            return name;
                        string message{ "parameter '" };
                        message.append( name ).append( "' is used with two types: " ).append( field._propertyType ).append( " and " ).append( kind.getParameterPropertyType() );
                        report( scope, node._name, "ParameterConflict", std::move( message ) );
                        return {};
                    }
                    MaterialGraphField field;
                    field._name         = name;
                    field._hlslType     = string{ kind.getParameterHLSLType() };
                    field._propertyType = string{ kind.getParameterPropertyType() };
                    field._assetPath    = node._assetPath;
                    if ( kind.findSetting( "Default" ) != nullptr )
                        field._defaultValue = readSettingText( kind, node, "Default" );
                    if ( kind.findSetting( "Min" ) != nullptr )
                        (void)StringUtil::parseFloat( readSettingText( kind, node, "Min" ), field._min ); // 등록과 값은 `expandSettings` 가 수치인지 함께 본다
                    if ( kind.findSetting( "Max" ) != nullptr )
                        (void)StringUtil::parseFloat( readSettingText( kind, node, "Max" ), field._max );
                    if ( kind.findSetting( "SRGB" ) != nullptr )
                        field._bSRGB = readSettingText( kind, node, "SRGB" ) != "0";
                    _listField.push_back( std::move( field ) );
                    return name;
                }

                [[nodiscard]] static string readSettingText( const MaterialGraphNodeKind& kind, const MaterialGraphNodeDef& node, string_view name )
                {
                    const MaterialGraphValue* pValue = node.findValue( name );
                    if ( pValue != nullptr )
                        return pValue->_value;
                    const MaterialGraphSettingDecl* pSetting = kind.findSetting( name );
                    return pSetting != nullptr ? string{ pSetting->_default } : string{};
                }

                /** @brief 수치 설정이 형식에 맞는지 봅니다(식에 쓰이지 않는 `Min` · `Default` 도 데이터의 오타를 잡는다). */
                void checkNumericSettings( const MaterialGraphNodeKind& kind, const MaterialGraphNodeDef& node, string_view scope )
                {
                    for ( uint32 index = 0; index < kind.getSettingCount(); ++index )
                    {
                        const MaterialGraphSettingDecl& setting = kind.getSetting( index );
                        if ( setting._type == MaterialGraphValueType::String )
                            continue;
                        Value        value;
                        const string text = readSettingText( kind, node, setting._name );
                        if ( makeLiteral( text, setting._type, value ) )
                            continue;
                        string message{ "setting '" };
                        message.append( setting._name ).append( "' value '" ).append( text ).append( "' is not " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( setting._type ) );
                        report( scope, node._name, "BadValue", std::move( message ) );
                    }
                }

                /** @brief 식의 `$핀` · `%설정` 을 펼칩니다. 모르는 이름은 등록의 잘못이라 문제를 남깁니다(단위 테스트가 모든 종류를 펼쳐 본다). */
                [[nodiscard]] string expand( string_view expression, const MaterialGraphNodeKind& kind, const vector<string>& listInput, const vector<NamedValue>& listOutput, const string& parameterName, const MaterialGraphNodeDef& node, string_view scope )
                {
                    string text;
                    for ( size_t position = 0; position < expression.size(); ++position )
                    {
                        const utf8 c = expression[position];
                        if ( c != '$' && c != '%' )
                        {
                            text.push_back( c );
                            continue;
                        }
                        size_t tokenEnd = position + 1;
                        while ( tokenEnd < expression.size() && isIdentifierChar( expression[tokenEnd] ) )
                        {
                            ++tokenEnd;
                        }
                        const string_view token = expression.substr( position + 1, tokenEnd - position - 1 );
                        position                = tokenEnd - 1;
                        bool bFound             = false;
                        if ( c == '$' )
                        {
                            const int32 inputIndex = kind.findInputIndex( token );
                            if ( inputIndex >= 0 )
                            {
                                text.append( listInput[static_cast<size_t>( inputIndex )] );
                                bFound = true;
                            }
                            for ( const NamedValue& output : listOutput )
                            {
                                if ( bFound == false && output._name == token )
                                {
                                    text.append( output._value._expression );
                                    bFound = true;
                                }
                            }
                        }
                        else if ( token == "Parameter" && kind.getParameterHLSLType().empty() == false )
                        {
                            text.append( "context._material." ).append( parameterName );
                            bFound = true;
                        }
                        else if ( kind.findSetting( token ) != nullptr )
                        {
                            Value value;
                            bFound = makeLiteral( readSettingText( kind, node, token ), kind.findSetting( token )->_type, value );
                            text.append( value._expression );
                        }
                        if ( bFound == false )
                        {
                            string message{ "the kind's expression names unknown '" };
                            message.append( token ).append( "'" );
                            report( scope, node._name, "BadSnippet", std::move( message ) );
                        }
                    }
                    return text;
                }

                /** @brief HLSL 조각 노드 하나를 펼칩니다. */
                void emitSnippet( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, const MaterialGraphNodeKind& kind, string_view scope, NodeResult& outResult )
                {
                    vector<Value> listValue;
                    bool          bAllRead = true;
                    for ( uint32 index = 0; index < kind.getInputCount(); ++index )
                    {
                        const MaterialGraphPinDecl& pin = kind.getInput( index );
                        listValue.push_back( resolveInput( graph, listResult, node, pin._name, pin._type, pin._default, scope ) );
                        bAllRead = bAllRead && listValue.back()._type != MaterialGraphValueType::Invalid;
                    }
                    checkNumericSettings( kind, node, scope );
                    const MaterialGraphValueType anyType = resolveAny( kind, listValue, node, scope );
                    vector<string>               listInput;
                    for ( uint32 index = 0; index < kind.getInputCount(); ++index )
                    {
                        const MaterialGraphPinDecl&  pin    = kind.getInput( index );
                        const MaterialGraphValueType wanted = pin._type == MaterialGraphValueType::Any ? anyType : pin._type;
                        string                       expression;
                        if ( listValue[index]._type != MaterialGraphValueType::Invalid && coerce( listValue[index], wanted, expression ) == false )
                        {
                            string message{ "pin '" };
                            message.append( pin._name ).append( "' wants " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( wanted ) ).append( " but got " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( listValue[index]._type ) );
                            report( scope, node._name, "TypeMismatch", std::move( message ) );
                            bAllRead = false;
                        }
                        listInput.push_back( std::move( expression ) );
                    }
                    string parameterName;
                    if ( kind.getParameterHLSLType().empty() == false )
                    {
                        parameterName = registerParameter( kind, node, scope );
                        bAllRead      = bAllRead && parameterName.empty() == false;
                    }
                    if ( bAllRead == false )
                        return; // 문제는 남겼다 — 이 노드의 출력은 비어 뒤 노드가 같은 문제를 다시 말하지 않는다

                    emitTarget().append( "    // " ).append( scope ).append( node._name ).append( " (" ).append( kind.getName() ).append( ")\n" );
                    const uint32 variable = _nextVariable++;
                    for ( uint32 index = 0; index < kind.getOutputCount(); ++index )
                    {
                        const MaterialGraphOutputDecl&         output = kind.getOutput( index );
                        const MaterialGraphValueType           type   = output._type == MaterialGraphValueType::Any ? anyType : output._type;
                        fixed_string<constant::buffer::kMax64> variableName;
                        formatstring( variableName.data(), variableName.capacity(), "n%#_%#", variable, output._name );
                        const string expression = expand( output._expression, kind, listInput, outResult._listOutput, parameterName, node, scope );
                        emitTarget().append( "    const " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( type ) ).append( " " ).append( variableName.c_str() ).append( " = " ).append( expression ).append( ";\n" );
                        outResult._listOutput.push_back( NamedValue{
                            string{ output._name },
                            Value{ string{ variableName.c_str() }, type }
                        } );
                    }
                }

                /** @brief 출력 노드입니다 — 입력 핀 이름이 표면 칸입니다(`BaseColor` → `output._baseColor`). */
                void emitOutput( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, const MaterialGraphNodeKind& kind, string_view scope )
                {
                    for ( uint32 index = 0; index < kind.getInputCount(); ++index )
                    {
                        const MaterialGraphPinDecl& pin   = kind.getInput( index );
                        const Value                 value = resolveInput( graph, listResult, node, pin._name, pin._type, pin._default, scope );
                        if ( value._type == MaterialGraphValueType::Invalid )
                            continue;
                        string expression;
                        if ( coerce( value, pin._type, expression ) == false )
                        {
                            string message{ "pin '" };
                            message.append( pin._name ).append( "' wants " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( pin._type ) ).append( " but got " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( value._type ) );
                            report( scope, node._name, "TypeMismatch", std::move( message ) );
                            continue;
                        }
                        string field{ pin._name };
                        field[0] = static_cast<utf8>( field[0] - 'A' + 'a' );
                        _outputLines.append( "    output._" ).append( field ).append( " = " ).append( expression ).append( ";\n" );
                    }
                    _bEmissive = node.findLink( "Emissive" ) != nullptr || node.findValue( "Emissive" ) != nullptr;
                    // 셰이딩 모델 — 빛을 받는 식을 고른다(언리얼 머티리얼의 Shading Model). Hair 는 hair_shading.hlsli 이고 G버퍼 칸이 없어 포워드 전용이다.
                    const string shadingModel = readSettingText( kind, node, "ShadingModel" );
                    _bHair                    = shadingModel == "Hair";
                    if ( _bHair == false && shadingModel != "DefaultLit" )
                    {
                        string message{ "ShadingModel '" };
                        message.append( shadingModel ).append( "' - must be DefaultLit or Hair" );
                        report( scope, node._name, "BadValue", std::move( message ) );
                    }
                }

                /** @brief 하위 그래프 노드를 펼칩니다(언리얼 Material Function 처럼 호출이 아니라 펼침이다 — 칸 · 순열이 머티리얼 하나로 모인다). */
                /** @brief 하위 그래프 · 층 노드가 가리키는 그래프(`_assetPath`)를 읽습니다. 경로 없음 · 순환 · 읽기 실패 · 하위 그래프가 아님은 문제를 남기고 false 입니다. */
                [[nodiscard]] bool loadSubgraph( const MaterialGraphNodeDef& node, string_view scope, MaterialGraphDef& outGraph )
                {
                    if ( node._assetPath.empty() )
                    {
                        report( scope, node._name, "MissingSubgraph", "subgraph node has no graph (_assetPath)" );
                        return false;
                    }
                    for ( const string& stacked : _listGraphStack )
                    {
                        if ( stacked != node._assetPath )
                            continue;
                        string message{ "subgraph cycle through " };
                        message.append( node._assetPath );
                        report( scope, node._name, "SubgraphCycle", std::move( message ) );
                        return false;
                    }
                    if ( outGraph.loadFromAsset( AssetID{ node._assetPath } ) == false )
                    {
                        string message{ "subgraph cannot be read: " };
                        message.append( node._assetPath );
                        report( scope, node._name, "SubgraphUnreadable", std::move( message ) );
                        return false;
                    }
                    if ( MaterialGraphCompileResult::isSubgraph( outGraph ) == false )
                    {
                        string message{ node._assetPath };
                        message.append( " has no GraphOutput node or has an Output node - it is not a subgraph" );
                        report( scope, node._name, "NotSubgraph", std::move( message ) );
                        return false;
                    }
                    return true;
                }

                void emitSubgraph( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, string_view scope, NodeResult& outResult )
                {
                    MaterialGraphDef subgraph;
                    if ( loadSubgraph( node, scope, subgraph ) == false )
                        return;
                    const MaterialGraphNodeKind* pInputKind = MaterialGraphNodeRegistry::find( "GraphInput" );
                    vector<NamedValue>           listArgument;
                    bool                         bAllRead = true;
                    for ( const MaterialGraphNodeDef& inner : subgraph._listNode )
                    {
                        if ( inner._kind != pInputKind->getName() )
                            continue;
                        const string                 pinName = readSettingText( *pInputKind, inner, "Name" );
                        const MaterialGraphValueType type    = MaterialGraphNodeRegistry::parseValueType( readSettingText( *pInputKind, inner, "Type" ) );
                        if ( static_cast<uint32>( type ) < 1 || static_cast<uint32>( type ) > 4 )
                        {
                            report( scope, inner._name, "BadValue", "GraphInput type must be float, float2, float3 or float4" );
                            bAllRead = false;
                            continue;
                        }
                        const Value value = resolveInput( graph, listResult, node, pinName, type, {}, scope );
                        string      expression;
                        if ( value._type == MaterialGraphValueType::Invalid )
                        {
                            bAllRead = false;
                            continue;
                        }
                        if ( coerce( value, type, expression ) == false )
                        {
                            string message{ "pin '" };
                            message.append( pinName ).append( "' wants " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( type ) ).append( " but got " ).append( MaterialGraphNodeRegistry::getHLSLTypeName( value._type ) );
                            report( scope, node._name, "TypeMismatch", std::move( message ) );
                            bAllRead = false;
                            continue;
                        }
                        listArgument.push_back( NamedValue{
                            pinName, Value{ std::move( expression ), type }
                        } );
                    }
                    if ( bAllRead == false )
                        return;
                    string innerScope{ scope };
                    innerScope.append( node._name ).append( "/" );
                    _listGraphStack.push_back( node._assetPath );
                    (void)emitGraph( subgraph, innerScope, listArgument, false, outResult._listOutput ); // 문제는 목록에 남았다
                    _listGraphStack.pop_back();
                }

                [[nodiscard]] static const LayerAttribute* findLayerAttribute( string_view pin )
                {
                    for ( const LayerAttribute& attribute : kArrLayerAttribute )
                    {
                        if ( attribute._pin == pin )
                            return &attribute;
                    }
                    return nullptr;
                }

                /** @brief 머티리얼 칸 하나를 더합니다. 같은 이름의 칸이 이미 있으면 문제를 남기고 false 입니다(층 칸은 노드 이름으로 정해져 겹치면 데이터의 잘못이다). */
                [[nodiscard]] bool addLayerField( MaterialGraphField field, const MaterialGraphNodeDef& node, string_view scope )
                {
                    for ( const MaterialGraphField& existing : _listField )
                    {
                        if ( existing._name != field._name )
                            continue;
                        string message{ "layer field '" };
                        message.append( field._name ).append( "' is already a field of the material" );
                        report( scope, node._name, "ParameterConflict", std::move( message ) );
                        return false;
                    }
                    _listField.push_back( std::move( field ) );
                    return true;
                }

                /** @brief 층 마스크 식입니다: 핀 `Mask` 를 이었거나 값을 적었으면 그 값, 아니면 `MaskChannel` 이 고른 `_layerMask` 의 채널(처음 쓸 때 한 번 읽는다), `None` 이면 1 입니다. 문제면 빈 글입니다. */
                [[nodiscard]] string makeLayerMask( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, const MaterialGraphNodeKind& kind, string_view scope )
                {
                    if ( node.findLink( "Mask" ) != nullptr || node.findValue( "Mask" ) != nullptr )
                    {
                        const Value value = resolveInput( graph, listResult, node, "Mask", MaterialGraphValueType::Float, {}, scope );
                        return value._type == MaterialGraphValueType::Invalid ? string{} : value._expression;
                    }
                    const string channel = readSettingText( kind, node, "MaskChannel" );
                    if ( channel == "None" )
                        return string{ "1.0f" };
                    const size_t channelIndex = channel.size() == 1 ? kMaskChannels.find( channel.front() ) : string_view::npos;
                    if ( channelIndex == string_view::npos || channelIndex >= _layerLimits._maxMaskChannelCount )
                    {
                        string message{ "MaskChannel '" };
                        message.append( channel ).append( "' must be None or one of the first " ).append( kMaskChannels.substr( 0, _layerLimits._maxMaskChannelCount ) ).append( " channels (" ).append( kLayerLimitsPath ).append( ")" );
                        report( scope, node._name, "BadValue", std::move( message ) );
                        return {};
                    }
                    if ( _bLayerMaskSampled == false )
                    {
                        MaterialGraphField field;
                        field._name         = string{ kLayerMaskField };
                        field._hlslType     = "uint";
                        field._propertyType = "Texture2D";
                        field._bSRGB        = false; // 마스크는 색이 아니라 비율이다
                        if ( addLayerField( std::move( field ), node, scope ) == false )
                            return {};
                        _outputLines.append( "    const float4 layerMask = MaterialTexture::sampleIndex( context._material._layerMask, context._uv );\n" );
                        _bLayerMaskSampled = true;
                    }
                    string expression{ "layerMask." };
                    expression.push_back( static_cast<utf8>( channel.front() - 'A' + 'a' ) );
                    return expression;
                }

                /**
                 * @brief 층 노드 하나를 펼쳐 출력 위에 얹습니다. 층 그래프(하위 그래프)의 `GraphOutput` 이 표면 칸이고 `GraphInput` 은 아래 칸을 읽습니다.
                 * @details 생성 길은 하위 그래프와 같다(펼침 — 런타임은 생성물만 읽는다). 층 안 파라미터는 `_<노드>` 를 머리에 붙인 칸이고, 칸 `_<노드>Weight` 가 마스크에 곱해진다 —
                 *          자식 `.material` 이 0 을 적으면 층이 꺼진다. 켜고 끄기를 값으로 두어 셰이더 · PSO 가 하나다(언리얼은 층 보이기를 정적 스위치로 다시 컴파일하고,
                 *          유니티 HDRP Layered Lit 은 층 수가 키워드다 — 기억에 의존). 층 파일을 바꾸는 변형은 셰이더가 달라지므로 그래프 `_base` 로 한다.
                 */
                void emitLayer( const MaterialGraphDef& graph, const vector<NodeResult>& listResult, const MaterialGraphNodeDef& node, const MaterialGraphNodeKind& kind, string_view scope )
                {
                    if ( isIdentifier( node._name ) == false )
                    {
                        report( scope, node._name, "BadName", "layer node name prefixes material fields - use letters, digits and '_'" );
                        return;
                    }
                    checkNumericSettings( kind, node, scope );
                    MaterialGraphDef layer;
                    if ( loadSubgraph( node, scope, layer ) == false )
                        return;

                    const MaterialGraphNodeKind* pInputKind = MaterialGraphNodeRegistry::find( "GraphInput" );
                    vector<NamedValue>           listArgument;
                    bool                         bAllRead = true;
                    for ( const MaterialGraphNodeDef& inner : layer._listNode )
                    {
                        if ( inner._kind != pInputKind->getName() )
                            continue;
                        const string          pinName    = readSettingText( *pInputKind, inner, "Name" );
                        const LayerAttribute* pAttribute = findLayerAttribute( pinName );
                        if ( pAttribute == nullptr || MaterialGraphNodeRegistry::parseValueType( readSettingText( *pInputKind, inner, "Type" ) ) != pAttribute->_type )
                        {
                            string message{ "layer input '" };
                            message.append( pinName ).append( "' must be a surface attribute below the layer with its type (BaseColor float3, Metallic float, Roughness float, Normal float3, Emissive float3)" );
                            report( scope, inner._name, "BadLayerPin", std::move( message ) );
                            bAllRead = false;
                            continue;
                        }
                        string below{ "output." };
                        below.append( pAttribute->_field );
                        listArgument.push_back( NamedValue{
                            pinName, Value{ std::move( below ), pAttribute->_type }
                        } );
                    }
                    const string mask = makeLayerMask( graph, listResult, node, kind, scope );
                    if ( bAllRead == false || mask.empty() )
                        return;

                    const string       weightName = "_" + node._name + "Weight";
                    MaterialGraphField weight;
                    weight._name         = weightName;
                    weight._hlslType     = "float";
                    weight._propertyType = "Range";
                    weight._defaultValue = readSettingText( kind, node, "Weight" );
                    if ( addLayerField( std::move( weight ), node, scope ) == false )
                        return;

                    string innerScope{ scope };
                    innerScope.append( node._name ).append( "/" );
                    const string previousPrefix = _parameterPrefix;
                    _parameterPrefix            = "_" + node._name;
                    _listGraphStack.push_back( node._assetPath );
                    vector<NamedValue> listLayerOutput;
                    const bool         bLayerEmitted = emitGraph( layer, innerScope, listArgument, false, listLayerOutput );
                    _listGraphStack.pop_back();
                    _parameterPrefix = previousPrefix;
                    if ( bLayerEmitted == false )
                        return;

                    const uint32                           variable = _nextVariable++;
                    fixed_string<constant::buffer::kMax64> maskName;
                    formatstring( maskName.data(), maskName.capacity(), "n%#_Mask", variable );
                    _outputLines.append( "    // " ).append( scope ).append( node._name ).append( " (Layer " ).append( node._assetPath ).append( ")\n" );
                    _outputLines.append( "    const float " ).append( maskName.c_str() ).append( " = saturate( " ).append( mask ).append( " * context._material." ).append( weightName ).append( " );\n" );
                    for ( const NamedValue& output : listLayerOutput )
                    {
                        const LayerAttribute* pAttribute = findLayerAttribute( output._name );
                        string                expression;
                        if ( pAttribute == nullptr || coerce( output._value, pAttribute->_type, expression ) == false )
                        {
                            string message{ "layer output '" };
                            message.append( output._name ).append( "' must be a surface attribute (BaseColor float3, Metallic float, Roughness float, Normal float3, Emissive float3)" );
                            report( scope, node._name, "BadLayerPin", std::move( message ) );
                            continue;
                        }
                        const string field{ pAttribute->_field };
                        const string blended = "lerp( output." + field + ", " + expression + ", " + maskName.c_str() + " )";
                        _outputLines.append( "    output." ).append( field ).append( " = " ).append( pAttribute->_field == "_normal" ? string{ "normalize( " + blended + " )" } : blended ).append( ";\n" );
                        _bEmissive = _bEmissive || pAttribute->_field == "_emissive";
                    }
                }

                /** @brief 층은 머티리얼 그래프에만 있고 수가 상한(`Config/Dev/MaterialLayers.json`) 안인지 봅니다. 상한은 층이 있을 때만 읽는다. */
                [[nodiscard]] bool validateLayers( const MaterialGraphDef& graph, const vector<uint32>& listLayer, bool bMaterial, string_view scope )
                {
                    if ( bMaterial == false )
                    {
                        report( scope, graph._listNode[listLayer.front()]._name, "LayerOutsideMaterial", "a Layer node belongs in a material graph, not in a subgraph or a layer" );
                        return false;
                    }
                    string error;
                    if ( loadLayerLimits( _layerLimits, error ) == false )
                    {
                        report( scope, "(graph)", "LayerLimitsUnreadable", std::move( error ) );
                        return false;
                    }
                    if ( listLayer.size() > _layerLimits._maxLayerCount )
                    {
                        string message{ "the material has more layers than _maxLayerCount in " };
                        message.append( kLayerLimitsPath );
                        report( scope, graph._listNode[listLayer[_layerLimits._maxLayerCount]]._name, "TooManyLayers", std::move( message ) );
                        return false;
                    }
                    return true;
                }

                /**
                 * @brief 그래프 하나를 펼칩니다. 머티리얼은 `Output` 이, 하위 그래프는 `GraphOutput` 들이 뿌리이고 뿌리에 닿지 않는 노드는 펼치지 않습니다(언리얼의 쓰이지 않는 노드와 같다).
                 * @param listArgument 하위 그래프의 입력 값
                 * @param outListGraphOutput 하위 그래프의 출력 값
                 * @return 이 그래프에서 새 문제가 없으면 true 입니다.
                 */
                [[nodiscard]] bool emitGraph( const MaterialGraphDef& graph, string_view scope, const vector<NamedValue>& listArgument, bool bMaterial, vector<NamedValue>& outListGraphOutput )
                {
                    const size_t issueCount = _listIssue.size();
                    if ( validateNodes( graph, scope ) == false )
                        return false;

                    const MaterialGraphNodeRole rootRole = bMaterial ? MaterialGraphNodeRole::Output : MaterialGraphNodeRole::GraphOutput;
                    vector<uint32>              listRoot;
                    vector<uint32>              listLayer;
                    for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( graph._listNode.size() ); ++nodeIndex )
                    {
                        const MaterialGraphNodeRole role = MaterialGraphNodeRegistry::find( graph._listNode[nodeIndex]._kind )->getRole();
                        if ( role == rootRole )
                            listRoot.push_back( nodeIndex );
                        if ( role == MaterialGraphNodeRole::Layer )
                            listLayer.push_back( nodeIndex );
                    }
                    if ( listRoot.empty() )
                    {
                        report( scope, "(graph)", "NoOutput", "the graph has no Output node" );
                        return false;
                    }
                    if ( bMaterial && listRoot.size() > 1 )
                    {
                        report( scope, graph._listNode[listRoot[1]]._name, "TwoOutputs", "the graph has more than one Output node" );
                        return false;
                    }
                    if ( listLayer.empty() == false && validateLayers( graph, listLayer, bMaterial, scope ) == false )
                        return false;
                    // 층은 출력 뒤에 노드 차례대로 쌓는다 — 뿌리 목록의 차례가 곧 층 차례다(언리얼 Material Layers 의 층 목록과 같은 자리).
                    listRoot.insert( listRoot.end(), listLayer.begin(), listLayer.end() );

                    // 링크가 간선이다(받는 노드가 내는 노드를 기다린다). 깊이 우선이라 링크 차례가 생성 HLSL 의 차례이고 내용 해시에 든다.
                    DependencyGraph dependencyGraph{ static_cast<uint32>( graph._listNode.size() ) };
                    for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( graph._listNode.size() ); ++nodeIndex )
                    {
                        for ( const MaterialGraphLink& link : graph._listNode[nodeIndex]._listLink )
                        {
                            dependencyGraph.addDependency( nodeIndex, static_cast<uint32>( graph.findNodeIndex( link._fromNode ) ) ); // 없는 노드는 `validateNodes` 가 거절했다
                        }
                    }
                    const DependencyOrder order = dependencyGraph.sortDepthFirst( listRoot );
                    if ( order.isSorted() == false )
                    {
                        string message{ "cycle: " };
                        for ( size_t step = 0; step < order._listCycle.size(); ++step )
                        {
                            message.append( step == 0 ? "" : " -> " ).append( graph._listNode[order._listCycle[step]]._name );
                        }
                        report( scope, graph._listNode[order._listCycle.front()]._name, "Cycle", std::move( message ) );
                        return false;
                    }

                    vector<NodeResult> listResult;
                    listResult.resize( graph._listNode.size() );
                    for ( const uint32 nodeIndex : order._listOrder )
                    {
                        const MaterialGraphNodeDef&  node = graph._listNode[nodeIndex];
                        const MaterialGraphNodeKind& kind = *MaterialGraphNodeRegistry::find( node._kind );
                        switch ( kind.getRole() )
                        {
                            case MaterialGraphNodeRole::Snippet:
                                emitSnippet( graph, listResult, node, kind, scope, listResult[nodeIndex] );
                                break;
                            case MaterialGraphNodeRole::Output:
                                emitOutput( graph, listResult, node, kind, scope );
                                _bLayerPhase = true; // 뒤에 오는 노드는 층과 그 마스크뿐이다(뿌리 차례)
                                break;
                            case MaterialGraphNodeRole::Layer:
                                emitLayer( graph, listResult, node, kind, scope );
                                break;
                            case MaterialGraphNodeRole::Subgraph:
                                emitSubgraph( graph, listResult, node, scope, listResult[nodeIndex] );
                                break;
                            case MaterialGraphNodeRole::GraphInput:
                            {
                                const string pinName = readSettingText( kind, node, "Name" );
                                for ( const NamedValue& argument : listArgument )
                                {
                                    if ( argument._name == pinName )
                                        listResult[nodeIndex]._listOutput.push_back( NamedValue{ string{ "Out" }, argument._value } );
                                }
                                break;
                            }
                            case MaterialGraphNodeRole::GraphOutput:
                            {
                                const Value value = resolveInput( graph, listResult, node, "Value", MaterialGraphValueType::Any, {}, scope );
                                if ( value._type != MaterialGraphValueType::Invalid )
                                    outListGraphOutput.push_back( NamedValue{ readSettingText( kind, node, "Name" ), value } );
                                break;
                            }
                        }
                    }
                    return _listIssue.size() == issueCount;
                }

                /** @brief 칸 선언입니다. 원소 크기가 16 의 배수여야 해 float4 를 앞에 두고 끝을 채운다(`ShaderBindingValidatorIssue::checkStructuredElementLayouts`). */
                [[nodiscard]] string makeFieldLines() const
                {
                    string text;
                    for ( const MaterialGraphField& field : _listField )
                    {
                        if ( field._hlslType == "float4" )
                            text.append( "    float4 " ).append( field._name ).append( ";\n" );
                    }
                    uint32 scalarCount = 0;
                    for ( const MaterialGraphField& field : _listField )
                    {
                        if ( field._hlslType == "float4" )
                            continue;
                        text.append( "    " ).append( field._hlslType ).append( " " ).append( field._name ).append( ";\n" );
                        ++scalarCount;
                    }
                    for ( uint32 padIndex = 0; scalarCount % 4 != 0; ++padIndex, ++scalarCount )
                    {
                        fixed_string<constant::buffer::kMax64> line;
                        formatstring( line.data(), line.capacity(), "    uint _swPad%#;\n", padIndex );
                        text.append( line.c_str() );
                    }
                    return text;
                }
            };
        };
    } // namespace

    bool MaterialGraphCompileResult::compile( const MaterialGraphDef& graph, string_view sourceName, MaterialGraphCompileResult& outResult )
    {
        outResult = MaterialGraphCompileResult{};
        MaterialGraphCompilerInternal::Session session;
        // 문턱값 칸은 늘 있다 — `alphaMode="Mask"` 는 자식 머티리얼이 적는 칸이고 셰이더는 그때도 같아야 한다(pbr_lit 과 같다).
        MaterialGraphField cutoff;
        cutoff._name         = "_alphaCutoff";
        cutoff._hlslType     = "float";
        cutoff._propertyType = "Range";
        cutoff._defaultValue = "0.5";
        session._listField.push_back( std::move( cutoff ) );

        vector<MaterialGraphCompilerInternal::NamedValue> listUnused;
        const bool                                        bEmitted = session.emitGraph( graph, {}, {}, true, listUnused );
        outResult._listIssue                                       = std::move( session._listIssue );
        outResult._listField                                       = std::move( session._listField );
        if ( bEmitted == false || outResult._listIssue.empty() == false )
            return false;
        // 셰이딩 모델 Hair 의 칸(hair_shading.hlsli 의 큐티클 이동 · 다중 산란, 가닥 축) — 그래프 핀이 아니라 머티리얼 칸이라 다른 그래프의 생성물이 바뀌지 않는다.
        if ( session._bHair )
            MaterialGraphCompilerInternal::appendHairFields( outResult._listField );

        MaterialGraphCompilerInternal::Session fieldLines;
        fieldLines._listField = outResult._listField;
        string& hlsl          = outResult._hlsl;
        hlsl.append( "// clang-format off\n// Generated from " ).append( sourceName ).append( " by MaterialGraphCompiler - edit the graph, not this file.\n" );
        hlsl.append( "#include \"lighting.hlsli\"\n\nSW_MATERIAL_BEGIN\n{\n" ).append( fieldLines.makeFieldLines() ).append( "}\nSW_MATERIAL_END\n\n" );
        if ( session._bEmissive )
            hlsl.append( "#define SW_MATERIAL_GRAPH_EMISSIVE\n" );
        if ( session._bHair )
            hlsl.append( "#define SW_MATERIAL_GRAPH_HAIR\n" );
        hlsl.append( "#include \"material_graph.hlsli\"\n\nMaterialGraphOutput evaluateMaterialGraph( PixelContext context )\n{\n" );
        hlsl.append( session._body ).append( "    MaterialGraphOutput output;\n" ).append( session._outputLines );
        hlsl.append( "    return output;\n}\n\n#include \"material_graph_main.hlsli\"\n" );

        outResult._bEmissive   = session._bEmissive;
        outResult._bHair       = session._bHair;
        outResult._contentHash = static_cast<uint32>( HashUtil::computeStringHash64( hlsl, false ) & 0xFFFFFFFFu );
        return true;
    }

    bool MaterialGraphCompileResult::isSubgraph( const MaterialGraphDef& graph )
    {
        bool bGraphOutput = false;
        for ( const MaterialGraphNodeDef& node : graph._listNode )
        {
            const MaterialGraphNodeKind* pKind = MaterialGraphNodeRegistry::find( node._kind );
            if ( pKind == nullptr )
                continue;
            if ( pKind->getRole() == MaterialGraphNodeRole::Output )
                return false;
            bGraphOutput = bGraphOutput || pKind->getRole() == MaterialGraphNodeRole::GraphOutput;
        }
        return bGraphOutput;
    }

    string MaterialGraphCompileResult::makeMaterialText( const MaterialGraphCompileResult& result, string_view materialName, string_view shaderPath, string_view sourceName )
    {
        string text;
        text.append( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- Generated from " ).append( sourceName ).append( " by MaterialGraphCompiler - edit the graph; change values in a child material with _base. -->\n" );
        text.append( "<MaterialDef formatVersion=\"0\" name=\"" ).append( materialName ).append( "\" shaderPath=\"" ).append( shaderPath ).append( "\" alphaMode=\"Opaque\">\n\t<_properties>\n" );
        for ( const MaterialGraphField& field : result._listField )
        {
            text.append( "\t\t<item name=\"" ).append( field._name ).append( "\" type=\"" ).append( field._propertyType ).append( "\"" );
            if ( field._defaultValue.empty() == false )
                text.append( " defaultValue=\"" ).append( field._defaultValue ).append( "\"" );
            if ( field._assetPath.empty() == false )
                text.append( " assetPath=\"" ).append( field._assetPath ).append( "\"" );
            if ( field._propertyType == "Range" )
                text.append( " min=\"" ).append( MaterialGraphCompilerInternal::formatPlainFloat( field._min ) ).append( "\" max=\"" ).append( MaterialGraphCompilerInternal::formatPlainFloat( field._max ) ).append( "\"" );
            if ( field._propertyType == "Color" )
                text.append( " bHdr=\"0\" bSrgb=\"1\"" );
            if ( field._propertyType == "Texture2D" )
                text.append( field._bSRGB ? " bSrgb=\"1\"" : " bSrgb=\"0\"" );
            text.append( " group=\"Graph\"/>\n" );
        }
        text.append( "\t</_properties>\n\t<_permutations quality=\"High\" shaderLOD=\"300\" usage=\"StaticMesh\">\n\t\t<_alwaysDefines>\n\t\t\t<item>MATERIAL_DOMAIN_SURFACE</item>\n" );
        // 자체 발광은 G버퍼에 칸이 없다 — 디퍼드가 이 배치를 빼고 ForwardOnlyOpaque 가 그린다(툰과 같은 길).
        if ( result._bEmissive || result._bHair )
            text.append( "\t\t\t<item>MATERIAL_FORWARD_ONLY</item>\n" );
        text.append( "\t\t</_alwaysDefines>\n\t\t<_multiCompiles>\n\t\t\t<item name=\"FogMode\" selected=\"FOG_OFF\">\n\t\t\t\t<_options>\n\t\t\t\t\t<item>FOG_OFF</item>\n\t\t\t\t\t<item>FOG_LINEAR</item>\n\t\t\t\t\t<item>FOG_EXP</item>\n\t\t\t\t</_options>\n\t\t\t</item>\n\t\t</_multiCompiles>\n" );
        text.append( "\t</_permutations>\n</MaterialDef>\n" );
        return text;
    }

    bool MaterialGraphCompileResult::collectNodePins( const MaterialGraphNodeDef& node, vector<MaterialGraphPinInfo>& outListInput, vector<MaterialGraphPinInfo>& outListOutput )
    {
        outListInput.clear();
        outListOutput.clear();
        const MaterialGraphNodeKind* pKind = MaterialGraphNodeRegistry::find( node._kind );
        if ( pKind == nullptr )
            return false;
        if ( pKind->getRole() == MaterialGraphNodeRole::Subgraph )
        {
            if ( node._assetPath.empty() )
                return true; // 아직 그래프를 고르지 않았다 — 핀이 없다
            MaterialGraphDef subgraph;
            if ( subgraph.loadFromAsset( AssetID{ node._assetPath } ) == false )
                return false;
            for ( const MaterialGraphNodeDef& inner : subgraph._listNode )
            {
                const MaterialGraphNodeKind* pInnerKind = MaterialGraphNodeRegistry::find( inner._kind );
                if ( pInnerKind == nullptr )
                    continue;
                if ( pInnerKind->getRole() == MaterialGraphNodeRole::GraphInput )
                {
                    const string typeName = MaterialGraphCompilerInternal::Session::readSettingText( *pInnerKind, inner, "Type" );
                    outListInput.push_back( MaterialGraphPinInfo{ MaterialGraphCompilerInternal::Session::readSettingText( *pInnerKind, inner, "Name" ), MaterialGraphNodeRegistry::parseValueType( typeName ) } );
                }
                if ( pInnerKind->getRole() == MaterialGraphNodeRole::GraphOutput )
                    outListOutput.push_back( MaterialGraphPinInfo{ MaterialGraphCompilerInternal::Session::readSettingText( *pInnerKind, inner, "Name" ), MaterialGraphValueType::Any } );
            }
            return true;
        }
        if ( pKind->getRole() == MaterialGraphNodeRole::GraphInput )
        {
            outListOutput.push_back( MaterialGraphPinInfo{ string{ "Out" }, MaterialGraphNodeRegistry::parseValueType( MaterialGraphCompilerInternal::Session::readSettingText( *pKind, node, "Type" ) ) } );
            return true;
        }
        for ( uint32 index = 0; index < pKind->getInputCount(); ++index )
        {
            outListInput.push_back( MaterialGraphPinInfo{ string{ pKind->getInput( index )._name }, pKind->getInput( index )._type } );
        }
        for ( uint32 index = 0; index < pKind->getOutputCount(); ++index )
        {
            outListOutput.push_back( MaterialGraphPinInfo{ string{ pKind->getOutput( index )._name }, pKind->getOutput( index )._type } );
        }
        return true;
    }
} // namespace sw
