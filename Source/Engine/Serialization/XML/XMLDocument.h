/**
 * @file XMLDocument.h
 * @brief 리플렉션 없는 XML 파싱·탐색 (콘텐츠 테이블, 맵, 툴)
 * @note 리플렉션 객체 그래프는 XMLSerializer / IXMLBackend를 사용합니다.
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
        /** @brief 빈(무효) 속성 핸들. */
        XMLAttribute() = default;

        /** @brief 속성이 유효하면 true. */
        bool isValid() const { return _pAttr != nullptr; }
        /** @brief isValid()와 동일. */
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
     * @brief XMLDocument 안의 가벼운 핸들입니다(clear/destroy 이후 무효).
     */
    class SW_API XMLNode
    {
    public:
        /** @brief 빈(무효) 노드 핸들. */
        XMLNode() = default;

        /** @brief 노드가 유효하면 true. */
        bool isValid() const { return _pNode != nullptr; }
        /** @brief isValid()와 동일. */
        explicit operator bool() const { return isValid(); }

        // ------------------------------------------------------------------------------
        // 1) 읽기 — 이름, 텍스트, 속성, 자식/형제
        // ------------------------------------------------------------------------------
        /** @brief 엘리먼트 이름을 반환합니다. */
        const utf8* getName() const;
        /** @brief 엘리먼트 텍스트입니다. 노드가 유효하면 빈 문자열이어도 nullptr 이 아닙니다. */
        const utf8* getText() const;
        /**
         * @brief 파싱한 원문에서 이 노드가 시작하는 바이트 위치입니다(오류에 줄 번호를 적을 때). 알 수 없으면(만든 노드 · 무효 노드) -1 입니다.
         * @details 줄 번호는 `XMLDocument::computeLineNumber( 원문, 위치 )` 로 셉니다.
         */
        int64 getSourceOffset() const;
        /** @brief 속성 값을 반환합니다. 없으면 nullptr. */
        const utf8* findAttribute( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /**
         * @brief 속성 값을 글로 반환합니다. 없으면 빈 글입니다.
         * @details `string_view` 를 받는 함수(`KeyCodeUtil::fromName` 등)에 넘길 때 씁니다 — `findAttribute` 의 nullptr 로 `string_view` 를 만들면 미정의 동작(strlen)입니다.
         */
        string_view getAttributeText( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 정수로 반환합니다. 정수가 아닌 글이면 알리고 `fallback` 입니다. */
        int32 getAttributeInt( const utf8* pName, int32 fallback = 0, bool bIgnoreCaseKeys = true ) const;
        /**
         * @brief 정수 속성을 `[minValue, maxValue]` 안에서 읽습니다. 속성이 없으면 `outValue` 는 `fallback` 이고 true 입니다.
         * @return 글이 정수가 아니거나 범위를 벗어나면 요소 · 속성 이름과 함께 경고하고 false 입니다(`outValue` 는 `fallback`).
         *         부르는 쪽은 그 값으로 만들던 것을 버립니다.
         * @details 좁은 칸에 `static_cast<uint8>( getAttributeInt( … ) )` 로 넣으면 "256" 이 0 으로, "-1" 이 255 로 감겨 말없이
         *          엉뚱한 값(다른 패드)이 됐습니다. 범위는 칸의 타입이 아니라 **뜻**으로 줍니다(패드 번호는 슬롯 수까지).
         */
        [[nodiscard]] bool tryGetAttributeIntInRange( const utf8* pName, int32 fallback, int32 minValue, int32 maxValue, int32& outValue,
                                                      bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 실수로 반환합니다. 실수가 아닌 글이면 알리고 `fallback` 입니다. */
        float32 getAttributeFloat( const utf8* pName, float32 fallback = 0.f, bool bIgnoreCaseKeys = true ) const;
        /** @brief 속성 값을 bool로 반환합니다 (1/true/yes/on · 0/false/no/off). 불리언이 아닌 글이면 알리고 `fallback` 입니다. */
        bool getAttributeBool( const utf8* pName, bool fallback = false, bool bIgnoreCaseKeys = true ) const;

        /** @brief 자식 노드를 찾습니다. pName 이 nullptr 이면 첫 자식입니다. */
        XMLNode findChild( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
        /** @brief 다음 형제 노드를 반환합니다. */
        XMLNode findNextSibling( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
        /** @brief 지정 이름 자식의 텍스트를 반환합니다. */
        const utf8* findChildText( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
        /** @brief 지정 이름 자식 텍스트를 정수로 반환합니다. */
        int32 getChildInt( const utf8* pName, int32 fallback = 0, bool bIgnoreCaseKeys = true ) const;
        /** @brief 지정 이름 자식 텍스트를 실수로 반환합니다. */
        float32 getChildFloat( const utf8* pName, float32 fallback = 0.f, bool bIgnoreCaseKeys = true ) const;
        /** @brief 지정 이름 자식 텍스트를 bool로 반환합니다. */
        bool getChildBool( const utf8* pName, bool fallback = false, bool bIgnoreCaseKeys = true ) const;

        /** @brief 비어 있지 않은 자식 텍스트를 dst에 복사합니다. 썼으면 true. */
        bool takeChildText( const utf8* pName, string& dst, bool bIgnoreCaseKeys = true ) const;

        /** @brief 첫 속성을 반환합니다. */
        XMLAttribute getFirstAttribute() const;

        // ------------------------------------------------------------------------------
        // 2) 쓰기 — 메모리는 문서 풀에서 할당
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
        /** @brief 숫자 속성을 추가합니다 (int32 · uint32 · float32). */
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
        /** @brief 숫자 속성을 설정합니다 (int32 · uint32 · float32). */
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
        /** @brief 노드 값을 숫자로 설정합니다 (int32 · uint32 · float32). */
        template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
        void setValue( T value ) const
        {
            setValue( formatNumber( value ).c_str() );
        }

        /** @brief 이 서브트리를 XML 문자열로 직렬화합니다 (Prefab/임베드용). */
        string toString() const;
        /** @brief 다른 문서의 서브트리를 이 노드의 자식으로 복사합니다. */
        XMLNode appendClone( XMLNode src ) const;

    private:
        /**
         * @brief 숫자를 XML 텍스트로 바꿉니다. 받는 타입을 셋으로 고정합니다 — 암묵 변환을 허용하면 int64 를 int32 로 잘라
         *        적어도 컴파일이 됩니다. 다른 타입은 여기서 컴파일 오류가 납니다.
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
        // 3) 수명 — 복사 금지, 이동 가능
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
        // 4) 파싱 · 로드
        // ------------------------------------------------------------------------------
        /**
         * @brief XML 전체 문서를 파싱합니다 (내부 버퍼에 복사).
         * @param sourceName 오류에 적는 이름(대개 경로)입니다. 비우면 `<memory>` 입니다.
         */
        [[nodiscard]] bool parse( string_view xmlText, string_view sourceName = {} );

        /** @brief 절대 경로를 읽고 파싱합니다. */
        [[nodiscard]] bool loadFile( string_view absPath );

        /** @brief 리소스 상대 경로를 해석한 뒤 읽고 파싱합니다. */
        [[nodiscard]] bool loadResource( string_view relativePath, string* pOutAbsPath = nullptr );

        /**
         * @brief 절대 경로 · 작업 경로에 파일이 있으면 loadFile, 없으면 loadResource 로 읽습니다.
         * @details 에셋 상대 경로와 에디터 절대 경로를 한 호출로 처리합니다.
         */
        [[nodiscard]] bool loadPath( string_view path, string* pOutAbsPath = nullptr );

        /**
         * @brief 마지막 parse · load 가 실패한 이유입니다(성공했으면 빈 문자열).
         * @details 구문 오류는 `경로:줄:열: 이유` 꼴이다 — IDE 터미널에서 눌러 그 자리로 간다. 파일이 없으면 `not found`, 읽지 못하면 `cannot read`.
         *          부르는 쪽(씬 · 프리팹)은 이것으로 구문 오류와 없는 파일을 가린다.
         */
        const string& getLastError() const { return _lastError; }
        /** @brief 원문 @p text 의 바이트 위치 @p offset 이 몇째 줄(1 부터)인지 셉니다. 위치가 음수면 0 입니다(`XMLNode::getSourceOffset` 이 모를 때). */
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
        // 6) 문자열 유틸 — escape, unescape
        // ------------------------------------------------------------------------------
        /** @brief XML 특수 문자(&, <, >, ", ')를 엔티티 문자열로 이스케이프합니다. */
        static string escapeString( string_view text );
        /** @brief XML 엔티티 문자열(&amp;, &lt;, &gt;, &quot;, &apos;)을 원래 문자로 복원합니다. */
        static string unescapeString( string_view text );

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
        string           _lastError; /**< 마지막 parse · load 의 실패 이유(`getLastError`) */
    };
} // namespace sw
