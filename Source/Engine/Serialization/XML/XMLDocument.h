/**
 * @file XMLDocument.h
 * @brief 리플렉션 없이 XML 을 파싱하고 탐색합니다. 콘텐츠 테이블, 맵, 툴에서 씁니다.
 * @note 리플렉션 객체 그래프는 XMLSerializer 나 IXMLBackend 를 사용합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    class XMLDocument; // Windows SDK msxml.h 의 전역 XMLDocument 대신 이것을 friend 로 찾게 먼저 선언한다

    /**
     * @class XMLAttribute
     * @brief XML 속성의 가벼운 핸들입니다.
     */
    class SW_API XMLAttribute
    {
    public:
        /** @brief 빈(무효) 속성 핸들을 만듭니다. */
        XMLAttribute() = default;

        /** @brief 속성이 유효하면 true 입니다. */
        bool isValid() const { return _pAttr != nullptr; }
        /** @brief isValid() 와 같습니다. */
        explicit operator bool() const { return isValid(); }

        /** @brief 속성 이름을 반환합니다. */
        const utf8* getName() const;
        /** @brief 속성 값을 반환합니다. */
        const utf8* getValue() const;
        /** @brief 다음 속성을 반환합니다. */
        XMLAttribute getNext() const;

    private:
        friend class XMLNode;
        /** @brief pugixml 속성 포인터로 핸들을 만듭니다. */
        explicit XMLAttribute( void* pAttr )
            : _pAttr{ pAttr } {}

        void* _pAttr{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLNode
     * @brief XMLDocument 안의 가벼운 핸들입니다. clear 나 destroy 이후에는 무효가 됩니다.
     */
    class SW_API XMLNode
    {
    public:
        /** @brief 빈(무효) 노드 핸들을 만듭니다. */
        XMLNode() = default;

        /** @brief 노드가 유효하면 true 입니다. */
        bool isValid() const { return _pNode != nullptr; }
        /** @brief isValid() 와 같습니다. */
        explicit operator bool() const { return isValid(); }

        // ------------------------------------------------------------------------------
        // 1) 읽기: 이름, 텍스트, 속성, 자식과 형제
        // ------------------------------------------------------------------------------
        /** @brief 엘리먼트 이름을 반환합니다. */
        const utf8* getName() const;
        /** @brief 엘리먼트 텍스트입니다. 노드가 유효하면 빈 문자열이어도 nullptr 이 아닙니다. */
        const utf8* getText() const;
        /**
         * @brief 파싱한 원문에서 이 노드가 시작하는 바이트 위치입니다. 오류에 줄 번호를 적을 때 씁니다. 알 수 없으면(직접 만든 노드, 무효 노드) -1 입니다.
         * @details 줄 번호는 `XMLDocument::computeLineNumber( 원문, 위치 )` 로 센다.
         */
        int64 getSourceOffset() const;
        /** @brief 속성 값을 반환합니다. 없으면 nullptr 입니다. */
        const utf8* findAttribute( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /**
         * @brief 속성 값을 문자열로 반환합니다. 없으면 빈 문자열입니다.
         * @details `string_view` 를 받는 함수(`KeyCodeUtil::fromName` 등)에 넘길 때 쓴다. `findAttribute` 가 반환한 nullptr 로 `string_view` 를 만들면 미정의 동작(strlen)이다.
         */
        string_view getAttributeText( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 정수로 반환합니다. 정수가 아닌 문자열이면 경고하고 `fallback` 을 반환합니다. */
        int32 getAttributeInt( const utf8* pName, int32 fallback = 0, bool bIgnoreCaseKeys = true ) const;
        /**
         * @brief 정수 속성을 `[minValue, maxValue]` 범위 안에서 읽습니다. 속성이 없으면 `outValue` 는 `fallback` 이고 true 를 반환합니다.
         * @return 문자열이 정수가 아니거나 범위를 벗어나면 요소와 속성 이름을 담아 경고하고 false 를 반환합니다(`outValue` 는 `fallback`).
         *         부르는 쪽은 그 값으로 만들던 것을 버립니다.
         * @details 좁은 필드에 `static_cast<uint8>( getAttributeInt( … ) )` 로 넣으면 "256" 은 0 으로, "-1" 은 255 로 감겨 말없이
         *          엉뚱한 값(다른 패드)이 된다. 범위는 필드의 타입이 아니라 **의미**로 준다(패드 번호는 슬롯 수까지).
         */
        [[nodiscard]] bool tryGetAttributeIntInRange( const utf8* pName, int32 fallback, int32 minValue, int32 maxValue, int32& outValue,
                                                      bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 실수로 반환합니다. 실수가 아닌 문자열이면 경고하고 `fallback` 을 반환합니다. */
        float32 getAttributeFloat( const utf8* pName, float32 fallback = 0.f, bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 bool 로 반환합니다(1/true/yes/on 과 0/false/no/off). 불리언이 아닌 문자열이면 경고하고 `fallback` 을 반환합니다. */
        bool getAttributeBool( const utf8* pName, bool fallback = false, bool bIgnoreCaseKeys = true ) const;

        /** @brief 자식 노드를 찾습니다. pName 이 nullptr 이면 첫 자식을 반환합니다. */
        XMLNode findChild( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
        /** @brief 다음 형제 노드를 반환합니다. */
        XMLNode findNextSibling( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
        /** @brief 이름이 일치하는 자식의 텍스트를 반환합니다. */
        const utf8* findChildText( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /** @brief 이름이 일치하는 자식의 텍스트를 정수로 반환합니다. */
        int32 getChildInt( const utf8* pName, int32 fallback = 0, bool bIgnoreCaseKeys = true ) const;
        /** @brief 이름이 일치하는 자식의 텍스트를 실수로 반환합니다. */
        float32 getChildFloat( const utf8* pName, float32 fallback = 0.f, bool bIgnoreCaseKeys = true ) const;
        /** @brief 이름이 일치하는 자식의 텍스트를 bool 로 반환합니다. */
        bool getChildBool( const utf8* pName, bool fallback = false, bool bIgnoreCaseKeys = true ) const;

        /** @brief 비어 있지 않은 자식 텍스트를 dst 에 복사합니다. 복사했으면 true 입니다. */
        bool takeChildText( const utf8* pName, string& dst, bool bIgnoreCaseKeys = true ) const;

        /** @brief 첫 속성을 반환합니다. */
        XMLAttribute getFirstAttribute() const;

        // ------------------------------------------------------------------------------
        // 2) 쓰기: 메모리는 문서 풀에서 할당한다
        // ------------------------------------------------------------------------------
        /** @brief 새 자식 노드를 추가합니다. */
        XMLNode appendChild( const utf8* pName ) const;
        /** @brief 새 자식 노드를 추가하고 값을 설정합니다. 값 타입은 `setValue` 가 받는 것이면 무엇이든 됩니다. */
        template <typename T>
        XMLNode appendChild( const utf8* pName, const T& value ) const
        {
            XMLNode childNode = appendChild( pName );
            childNode.setValue( value );
            return childNode;
        }

        /** @brief 새 속성을 추가합니다. */
        void appendAttribute( const utf8* pName, const utf8* pValue ) const;
        /** @brief 새 속성을 추가합니다. */
        void appendAttribute( const utf8* pName, string_view value ) const;
        /** @brief bool 속성(1/0)을 추가합니다. */
        void appendAttribute( const utf8* pName, bool value ) const;
        /** @brief 숫자 속성을 추가합니다(int32, uint32, float32). */
        template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
        void appendAttribute( const utf8* pName, T value ) const
        {
            appendAttribute( pName, formatNumber( value ).c_str() );
        }

        /** @brief 기존 속성 값을 바꾸거나, 없으면 추가합니다. */
        void setAttribute( const utf8* pName, const utf8* pValue ) const;
        /** @brief 기존 속성 값을 바꾸거나, 없으면 추가합니다. */
        void setAttribute( const utf8* pName, string_view value ) const;
        /** @brief bool 속성(1/0)을 설정합니다. */
        void setAttribute( const utf8* pName, bool value ) const;
        /** @brief 숫자 속성을 설정합니다(int32, uint32, float32). */
        template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
        void setAttribute( const utf8* pName, T value ) const
        {
            setAttribute( pName, formatNumber( value ).c_str() );
        }

        /** @brief 노드 이름을 설정합니다. */
        void setName( const utf8* pName ) const;
        /** @brief 노드 값을 설정합니다. */
        void setValue( const utf8* pValue ) const;
        /** @brief 노드 값을 설정합니다. */
        void setValue( string_view value ) const;
        /** @brief 노드 값을 bool(1/0)로 설정합니다. */
        void setValue( bool value ) const;
        /** @brief 노드 값을 숫자로 설정합니다(int32, uint32, float32). */
        template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
        void setValue( T value ) const
        {
            setValue( formatNumber( value ).c_str() );
        }

        /** @brief 이 서브트리를 XML 문자열로 직렬화합니다(Prefab 이나 임베드용). */
        string toString() const;
        /** @brief 다른 문서의 서브트리를 이 노드의 자식으로 복사합니다. */
        XMLNode appendClone( XMLNode src ) const;

    private:
        /**
         * @brief 숫자를 XML 텍스트로 바꿉니다. 받는 타입을 세 가지로 고정합니다. 암묵 변환을 허용하면 int64 를 int32 로 잘라
         *        적어도 컴파일이 되기 때문입니다. 다른 타입은 여기서 컴파일 오류가 납니다.
         */
        template <typename T>
        static StringBuilder<constant::kMaxBuffer32> formatNumber( T value )
        {
            static_assert( std::is_same_v<T, int32> || std::is_same_v<T, uint32> || std::is_same_v<T, float32>,
                           "XMLNode number text: int32 / uint32 / float32 only" );
            StringBuilder<constant::kMaxBuffer32> sb;
            sb.append( value );
            return sb;
        }

    private:
        friend class XMLDocument;
        /** @brief pugixml 노드 포인터로 핸들을 만듭니다. */
        explicit XMLNode( void* pNode )
            : _pNode{ pNode } {}

        void* _pNode{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLDocument
     * @brief pugixml 문서 트리입니다. TypeInfo 없이 손으로 읽을 때 씁니다.
     */
    class SW_API XMLDocument
    {
    public:
        // ------------------------------------------------------------------------------
        // 3) 수명: 복사 금지, 이동 가능
        // ------------------------------------------------------------------------------
        /** @brief 빈 문서를 만듭니다. */
        XMLDocument();
        /** @brief 파싱한 문서를 해제합니다. */
        ~XMLDocument();

        /** @brief 복사를 금지합니다. */
        XMLDocument( const XMLDocument& ) = delete;
        /** @brief 대입을 금지합니다. */
        XMLDocument& operator=( const XMLDocument& ) = delete;
        /** @brief 문서를 이동합니다. */
        XMLDocument( XMLDocument&& ) noexcept;
        /** @brief 문서를 이동 대입합니다. */
        XMLDocument& operator=( XMLDocument&& ) noexcept;

        /** @brief 문서를 비우고 파싱된 데이터를 해제합니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 4) 파싱과 로드
        // ------------------------------------------------------------------------------
        /**
         * @brief XML 전체 문서를 파싱합니다(내부 버퍼에 복사합니다).
         * @param sourceName 오류에 적는 이름(대개 경로)입니다. 비우면 `<memory>` 입니다.
         */
        [[nodiscard]] bool parse( string_view xmlText, string_view sourceName = {} );

        /** @brief 디스크의 절대 경로를 그대로 읽고 파싱합니다. 팩을 보지 않습니다(도구 · 설정 · Saved 파일용). */
        [[nodiscard]] bool loadFile( string_view absPath );

        /**
         * @brief 리소스 id · OS 절대 경로 · 작업 경로를 `ResourceIO::readTextResource` 로 읽고 파싱합니다(낱개 파일 · 팩).
         * @details 디스크를 먼저 보는 별도 입구를 두지 않는다 — 낱개 파일 허용 정책은 `ResourceIO` 한 곳이 본다.
         */
        [[nodiscard]] bool loadResource( string_view relativePath, string* pOutAbsPath = nullptr );

        /**
         * @brief 마지막 parse 나 load 가 실패한 이유입니다. 성공했으면 빈 문자열입니다.
         * @details 구문 오류는 `경로:줄:열: 이유` 형태다. IDE 터미널에서 눌러 그 위치로 이동할 수 있다. 파일이 없으면 `not found`, 읽지 못하면 `cannot read` 다.
         *          부르는 쪽(씬, 프리팹)은 이것으로 구문 오류와 없는 파일을 구분한다.
         */
        const string& getLastError() const { return _lastError; }
        /** @brief 원문 @p text 의 바이트 위치 @p offset 이 몇 번째 줄(1 부터)인지 셉니다. 위치가 음수면 0 입니다(`XMLNode::getSourceOffset` 이 모를 때). */
        static uint32 computeLineNumber( string_view text, int64 offset );

        /** @brief 첫 엘리먼트를 반환합니다. pName 이 있으면 이름으로 찾습니다(기본은 대소문자 무시). */
        XMLNode getRoot( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;

        // ------------------------------------------------------------------------------
        // 5) 쓰기
        // ------------------------------------------------------------------------------
        /** @brief 루트 노드를 만들고 반환합니다. */
        XMLNode appendRoot( const utf8* pName );
        /** @brief 현재 문서를 XML 문자열로 직렬화합니다. */
        string saveToString() const;
        /** @brief 현재 문서를 절대 경로에 씁니다. */
        [[nodiscard]] bool saveFile( string_view absPath ) const;

        // ------------------------------------------------------------------------------
        // 6) 문자열 유틸: escape, unescape
        // ------------------------------------------------------------------------------
        /** @brief XML 특수 문자(&, <, >, ", ')를 엔티티 문자열로 이스케이프합니다. */
        static string escapeString( string_view text );
        /** @brief XML 엔티티 문자열(&amp;, &lt;, &gt;, &quot;, &apos;)을 원래 문자로 복원합니다. */
        static string unescapeString( string_view text );

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
        string           _lastError; /**< 마지막 parse 나 load 의 실패 이유(`getLastError`) */
    };
} // namespace sw
