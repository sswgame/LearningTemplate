/**
 * @file XMLReadUtil.h
 * @brief 손으로 읽는 데이터 XML 의 공통 읽기 — 문서 · 루트 찾기, 이름 · 이름 목록, 실수 묶음, 범위 있는 숫자 · 불리언 속성입니다(커브는 `GameDataXML::readFloatCurve` — 같은 티어인 Utility 를 못 본다).
 * @details 캐릭터 데이터(`CharacterDataReader`), 게임 데이터 표(`GameDataXML`), 키트 데이터가 같은 몸통을 각자 적던 것을 한 곳에 둡니다.
 *          앞 묶음(`loadRoot` · `parseRoot` · `readOptionalName` · `readFloatList`)은 무엇이 실패했는지만 돌려주고 알리지 않습니다(오류 모음에 담는 쪽용).
 *          뒤 묶음(`…OrWarn` · `readRequired…` · `readOptional…`)은 원소 · 속성 이름과 함께 로그로 알리고 bool 을 돌려줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class XMLDocument;
    class XMLNode;

    /**
     * @enum XMLRootStatus
     * @brief `XMLReadUtil::loadRoot` · `parseRoot` 의 결과입니다.
     */
    enum class XMLRootStatus : uint8
    {
        Found,       ///< 루트 원소를 찾았다
        NotLoaded,   ///< 읽지 못했거나 구문 오류다 — 이유는 `XMLDocument::getLastError`
        RootMissing, ///< 문서는 읽었지만 그 이름의 루트 원소가 없다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct XMLReadUtil
     * @brief 데이터 XML 읽기 도우미입니다(전부 static). 알리는지는 함수 묶음마다 다르다 — 파일 머리 주석을 본다.
     */
    struct SW_API XMLReadUtil
    {
        /**
         * @brief 경로(리소스 상대 또는 절대)의 문서를 읽고 루트 원소를 찾습니다.
         * @param pRootName 루트 원소 이름입니다(철자 그대로).
         * @param pOutAbsPath 있으면 실제로 읽은 경로를 받습니다(`XMLDocument::loadResource`).
         */
        [[nodiscard]] static XMLRootStatus loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot,
                                                     string* pOutAbsPath = nullptr );
        /** @brief 텍스트를 파싱하고 루트 원소를 찾습니다. @p sourceName 은 구문 오류에 적는 이름입니다. */
        [[nodiscard]] static XMLRootStatus parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                                      XMLNode& outRoot );

        /**
         * @brief **선택** 이름 속성을 앞뒤 공백을 뺀 이름으로 읽습니다. 없거나 비었으면(공백뿐이어도) 빈 이름입니다.
         * @details 필수 이름은 이 함수로 읽지 않는다 — 빈 이름이 "적지 않음"과 "잘못 적음"을 가르지 못해, 부르는 쪽이 비었는지 보는 것을 잊으면 조용히 지나간다.
         *          필수는 오류를 남기는 읽기(`readRequiredName` · `AppearanceXMLUtil::readRequiredName` · `CharacterDataReader::readName( …, true )`)를 쓴다.
         *          글이 빈 이름의 철자(`none`, 대소문자 무시)이면 오류를 알리고 빈 이름을 돌려준다 — 이름은 "적지 않음"과 값 "none" 을 가를 수 없다.
         *          그 글이 뜻을 가지는 속성(열거형 · 경로)은 이름이 아니라 글(`XMLNode::findAttribute`)로 읽는다. 빈 이름의 `c_str()` 도 "None" 이라 경로로 쓰면 안 된다.
         */
        static hashed_string readOptionalName( const XMLNode& node, const utf8* pAttribute );
        /**
         * @brief 요소의 속성과 자식 요소 이름이 모두 @p listAttribute · @p listChild 안에 있는지 봅니다(대소문자까지 맞아야 한다). 모르는 것마다 @p context 와 이름을 오류로 남기고 false 를 반환합니다.
         * @details 데이터가 조용히 기본값이 되지 않게 한다. 철자가 틀린 속성 · 자식은 읽히지 않은 채 지나가기 때문이다(JSON 시절의 `AnimationJSONUtil::hasOnlyKnownKeys` 와 같다).
         */
        [[nodiscard]] static bool hasOnlyKnownNames( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string_view context );
        /** @brief `1,2,3` 처럼 쉼표나 공백으로 가른 숫자 글을 정확히 @p count 개 읽습니다. 개수가 다르거나 숫자가 아닌 토큰이 있으면 false 이고 @p pOutValue 는 그대로입니다. */
        [[nodiscard]] static bool readFloatList( string_view text, float32* pOutValue, uint32 count );
        /** @brief 숫자 @p count 개를 `readFloatList` 가 읽는 글(쉼표로 이은 가장 짧은 표기)로 씁니다. */
        static string makeFloatListText( const float32* pValue, uint32 count );

        // ------------------------------------------------------------------------------
        // 알리는 읽기 — 아래는 실패를 경고 · 오류 로그로 남기고 bool 을 돌려준다(게임 데이터 표 · 키트 데이터가 같은 문구로 알리게).
        // ------------------------------------------------------------------------------

        /** @brief `loadRoot` 를 하고 실패하면 경고합니다. @p outSourceName 은 실제로 읽은 경로가 되어 이후 경고가 어느 파일인지 가리킨다. */
        [[nodiscard]] static bool loadRootOrWarn( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName );
        /** @brief `parseRoot` 를 하고 실패하면 경고합니다. 테스트와 에디터 미리보기가 씁니다. */
        [[nodiscard]] static bool parseRootOrWarn( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot );

        /** @brief 원소 이름 @p name 이 @p pElement 와 대소문자를 무시하고 같은지 답합니다. 원소 이름으로 갈라 읽을 때 씁니다. */
        [[nodiscard]] static bool isElement( string_view name, const utf8* pElement ) { return StringUtil::equals( name, string_view( pElement ), true ); }
        /** @brief `id` 속성을 반환합니다. 없거나 비면 "<원소> without an id - skipped" 를 경고하고 nullptr 를 반환합니다. */
        static const utf8* findRequiredID( const XMLNode& node, string_view sourceName );
        /**
         * @brief **필수** 이름 속성 @p pAttribute 를 읽어 @p outName 에 넣습니다. 속성이 없거나 비었거나 글 `none` 이면 원소 · 속성 이름과 함께 오류를 알리고 false 입니다.
         * @details 반환값을 버릴 수 없다(`[[nodiscard]]`) — 호출부가 "이 원소를 넣을지"를 정한다. @p outName 은 실패하면 빈 이름이다.
         */
        [[nodiscard]] static bool readRequiredName( const XMLNode& node, const utf8* pAttribute, string_view sourceName, hashed_string& outName );

        /**
         * @brief @p separators 의 아무 글자로 나뉜 이름 목록을 @p outListName 에 채웁니다. 먼저 목록을 비웁니다.
         * @details 데이터마다 구분자가 다르다(",; " · ",;| " · ",; \t"). 기존 항목을 두고 덧붙이려면 `appendNameList` 를 쓴다. 빈 이름의 철자(`none`)는 오류로 알리고 넣지 않는다.
         */
        static void parseNameList( string_view text, string_view separators, vector<hashed_string>& outListName );
        /** @brief `parseNameList` 와 같지만 목록을 비우지 않고 뒤에 덧붙입니다. */
        static void appendNameList( string_view text, string_view separators, vector<hashed_string>& outListName );

        /**
         * @brief 쉼표, 공백, 탭, 세미콜론으로 나뉜 실수를 하나 이상 @p maxCount 개 이하로 읽습니다.
         * @details 하나도 없거나 @p maxCount 를 넘거나 숫자가 아닌 토큰이 있으면 false 이고 @p outListValue 는 그대로입니다. 개수가 정해져 있으면 `parseFloat2` · `parseFloat3` · `parseFloat4` 를 쓴다.
         */
        [[nodiscard]] static bool parseFloatList( string_view text, uint32 maxCount, vector<float32>& outListValue );
        /**
         * @brief "x y" 처럼 정확히 두 실수를 읽습니다(쉼표 · 공백 · 탭 · 세미콜론). 개수가 모자라거나 남거나 숫자가 아닌 토큰이 있으면 false 이고 @p outValue 는 그대로입니다.
         * @details 빠진 성분을 기본값으로 메우지 않으므로 호출부가 실패를 로드 오류로 올려야 한다. `parseFloat3` · `parseFloat4` 도 같다.
         */
        [[nodiscard]] static bool parseFloat2( string_view text, float2& outValue );
        /** @brief "x y z" 처럼 정확히 세 실수를 읽습니다. 규칙은 `parseFloat2` 와 같습니다. */
        [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue );
        /** @brief "r g b a" 처럼 정확히 네 실수를 읽습니다. 규칙은 `parseFloat2` 와 같습니다. */
        [[nodiscard]] static bool parseFloat4( string_view text, float4& outValue );

        /**
         * @brief 선택 속성 @p pAttribute 를 "x y" 두 실수로 읽습니다. 속성이 없으면 @p inoutValue 를 그대로 두고 true 입니다(호출부가 초기값을 정한다).
         * @details 속성이 있는데 정확히 두 실수가 아니면 속성 이름과 함께 오류를 알리고 false 이며 @p inoutValue 는 그대로입니다. `readOptionalFloat3` 도 같다.
         */
        [[nodiscard]] static bool readOptionalFloat2( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float2& inoutValue );
        /** @brief 선택 속성 @p pAttribute 를 "x y z" 세 실수로 읽습니다. 규칙은 `readOptionalFloat2` 와 같습니다. */
        [[nodiscard]] static bool readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& inoutValue );
        /** @brief 필수 속성 @p pAttribute 를 "x y z" 세 실수로 읽습니다. 속성이 없거나 정확히 세 실수가 아니면 오류를 알리고 false 입니다. */
        [[nodiscard]] static bool readRequiredFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& outValue );

        /**
         * @brief 선택 숫자 속성을 손으로 읽습니다(스키마 구조체가 없는 손 읽기용 — 카탈로그 원소는 `GameDataXML::readFields`).
         * @details 없으면 @p inoutValue 를 두고 true 다 — 기본값은 부르는 쪽 Def 의 초기값이고 여기서 정하지 않는다.
         *          깨진 값이나 [@p minValue, @p maxValue] 밖 값은 원소 · 속성 이름과 함께 오류이고 false 이며 @p inoutValue 는 그대로다(자르지 않는다).
         */
        [[nodiscard]] static bool readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, int32 minValue, int32 maxValue, int32& inoutValue );
        /** @brief `readOptionalNumber` 의 실수 버전입니다. */
        [[nodiscard]] static bool readOptionalNumber( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float32 minValue, float32 maxValue,
                                                      float32& inoutValue );
        /** @brief 선택 불리언 속성을 손으로 읽습니다. 없으면 @p inoutValue 를 두고 true, 깨졌으면 오류와 false 입니다. */
        [[nodiscard]] static bool readOptionalBool( const XMLNode& node, const utf8* pAttribute, bool& inoutValue );
        /** @brief `readOptionalBool` 의 `uint8` 표시(`SW_TRUE` · `SW_FALSE`) 버전입니다. */
        [[nodiscard]] static bool readOptionalFlag( const XMLNode& node, const utf8* pAttribute, uint8& inoutFlag );

        /** @brief `forEachNamedNumber` 에서 토큰에 콜론(`:값`)이 없을 때의 처리입니다. */
        enum class MissingValue : uint8
        {
            UseDefault, //!< 기본값으로 읽고 유효로 친다("stance" = "stance:1").
            Reject,     //!< 값이 꼭 있어야 한다. 유효하지 않은 토큰으로 넘긴다.
        };

        /**
         * @brief "이름:숫자" 토큰 목록("spearman:3,archer:2")을 읽어 토큰마다 @p callback( token, name, value, bValid ) 을 부릅니다. 할당하지 않습니다.
         * @details @p TValue 는 `int32` 또는 `float32` 이다(`StringUtil::parseInt` · `parseFloat`). 숫자가 읽히지 않거나 @p missing 이 `Reject` 인데 콜론이 없으면
         *          @p value 는 @p defaultValue, @p bValid 는 false 다. 경고와 범위 제한은 호출부가 한다(경고에 카탈로그 문맥이 들어가야 한다).
         * @code
         *     XMLReadUtil::forEachNamedNumber( text, ",; ", 1, XMLReadUtil::MissingValue::UseDefault,
         *                                      [&]( string_view token, string_view name, int32 count, bool bValid ) { ... } );
         * @endcode
         */
        template <typename TValue, typename TCallback>
        static void forEachNamedNumber( string_view text, string_view separators, TValue defaultValue, MissingValue missing, TCallback&& callback )
        {
            static_assert( std::is_same_v<TValue, int32> || std::is_same_v<TValue, float32>, "forEachNamedNumber reads int32 or float32" );
            StringUtil::forEachToken( text, separators, [&]( string_view token )
            {
                const size_t      colon = token.find( ':' );
                const string_view name  = colon == string_view::npos ? token : token.substr( 0, colon );
                TValue            value = defaultValue;
                bool              bValid{ missing == MissingValue::UseDefault };
                if ( colon != string_view::npos )
                {
                    if constexpr ( std::is_same_v<TValue, int32> )
                        bValid = StringUtil::parseInt( token.substr( colon + 1 ), value );
                    else
                        bValid = StringUtil::parseFloat( token.substr( colon + 1 ), value );
                    if ( bValid == false )
                        value = defaultValue;
                }
                callback( token, name, value, bValid );
            } );
        }
    };
} // namespace sw
