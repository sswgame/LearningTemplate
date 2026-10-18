/**
 * @file JSONDocument.h
 * @brief 리플렉션 없이 JSON 을 파싱하고 탐색합니다. 콘텐츠 테이블, 맵, 툴에서 씁니다.
 * @note 리플렉션 객체 그래프는 JSONSerializer 를 사용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include <optional>

namespace sw
{
    /**
     * @enum JSONType
     * @brief JSON 값의 종류입니다.
     */
    enum class JSONType : uint8
    {
        Null = 0,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    /**
     * @class JSONValue
     * @brief JSONDocument 안의 가벼운 핸들입니다. clear 나 destroy 이후에는 무효가 됩니다.
     * @warning **같은 부모에 항목을 추가하면 앞서 받은 핸들이 무효가 됩니다.** 이 핸들은 문서 안의
     *          노드를 가리키는 **빌린 포인터**이고, 객체도 배열도 연속 저장(`std::vector`)이라
     *          `set( 새 키 )` 나 `pushBack()` 이 재할당을 일으키면 그 부모에서 앞서 꺼낸 핸들이
     *          해제된 메모리를 가리킵니다. 안전한 쓰기 순서는 **하나를 받아 다 채운 뒤 다음 것을
     *          받는 것**입니다. 형제 핸들을 여럿 보관해 번갈아 쓰지 마십시오. 읽기(`get`/`at`)만
     *          하는 동안에는 문서가 변하지 않으므로 여러 핸들을 보관해도 됩니다.
     */
    class SW_API JSONValue
    {
    public:
        /** @brief 빈(무효) 값 핸들을 만듭니다. */
        JSONValue() = default;

        /** @brief 값이 유효하면 true 입니다. */
        bool isValid() const { return _pValue != nullptr; }
        /** @brief isValid() 와 같습니다. */
        explicit operator bool() const { return isValid(); }

        // ------------------------------------------------------------------------------
        // 1) 읽기: 타입, 스칼라, 객체와 배열
        // ------------------------------------------------------------------------------
        /** @brief JSON 타입을 반환합니다. 무효이면 Null 입니다. */
        JSONType getType() const;
        /** @brief 객체이면 true 입니다. */
        bool isObject() const { return getType() == JSONType::Object; }
        /** @brief 배열이면 true 입니다. */
        bool isArray() const { return getType() == JSONType::Array; }
        /** @brief 문자열이면 true 입니다. */
        bool isString() const { return getType() == JSONType::String; }
        /** @brief 숫자이면 true 입니다. */
        bool isNumber() const { return getType() == JSONType::Number; }
        /** @brief 불리언이면 true 입니다. */
        bool isBool() const { return getType() == JSONType::Bool; }
        /** @brief null 이면 true 입니다. */
        bool isNull() const { return getType() == JSONType::Null; }

        /** @brief 문자열 내용을 반환합니다. 문자열이 아니면 스칼라를 dump 한 값(따옴표 없음)을 반환합니다. */
        string asString() const;
        /** @brief 정수로 읽습니다. */
        int64 asInt( int64 fallback = 0 ) const;
        /** @brief 부호 없는 정수로 읽습니다. */
        uint64 asUint( uint64 fallback = 0 ) const;
        /** @brief 실수로 읽습니다. */
        float64 asFloat( float64 fallback = 0.0 ) const;
        /** @brief 불리언으로 읽습니다. */
        bool asBool( bool fallback = false ) const;

        /** @brief 객체 멤버 수 또는 배열 원소 수를 반환합니다. */
        size_t size() const;
        /** @brief 객체 멤버 이름을 삽입 순으로 반환합니다. */
        vector<string> getMemberNames() const;
        /** @brief 객체 멤버를 찾습니다. 없으면 무효 핸들을 반환합니다. */
        JSONValue get( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 객체에 키가 있으면 true 입니다. */
        bool has( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 배열 원소를 반환합니다. 범위 밖이면 무효 핸들을 반환합니다. */
        JSONValue at( size_t index ) const;

        /**
         * @brief 반드시 있어야 하는 객체 멤버 @p key 를 타입 T 로 읽습니다. 없거나 타입이 다르면(정수 칸의 실수 · 범위 밖 포함) 키와 함께 오류를 남기고 nullopt 입니다.
         * @details 폴백 인자를 받지 않는다(결정 기록 5-4). T 는 int32 · uint32 · int64 · uint64 · float32 · float64 · bool · string 입니다.
         */
        template <typename T>
        [[nodiscard]] std::optional<T> readRequired( string_view key ) const
        {
            T                   value{};
            const JSONValueRead read = parseMember( key, value );
            if ( read == JSONValueRead::Absent )
                reportMissing( key );
            return read == JSONValueRead::Read ? std::optional<T>{ value } : std::nullopt;
        }
        /** @brief 없어도 되는 객체 멤버 @p key 를 읽습니다. 없으면(또는 null) @p outValue 를 비우고 true, 타입이 다르면 오류와 false 입니다. */
        template <typename T>
        [[nodiscard]] bool readOptional( string_view key, std::optional<T>& outValue ) const
        {
            T                   value{};
            const JSONValueRead read = parseMember( key, value );
            outValue                 = read == JSONValueRead::Read ? std::optional<T>{ value } : std::nullopt;
            return read != JSONValueRead::Invalid;
        }

        /** @brief 이 서브트리를 JSON 문자열로 직렬화합니다. indent 가 음수이면 한 줄로 만듭니다. */
        string dump( int32 indent = -1 ) const;
        /** @brief 다른 핸들의 값을 이 노드에 복사합니다. */
        void assignFrom( const JSONValue& other ) const;

        // ------------------------------------------------------------------------------
        // 2) 쓰기: 문서는 호출자가 소유한다
        // ------------------------------------------------------------------------------
        /** @brief null 로 만듭니다. */
        void setNull() const;
        /** @brief 불리언으로 만듭니다. */
        void setBool( bool value ) const;
        /** @brief 정수로 만듭니다. */
        void setInt( int64 value ) const;
        /** @brief 부호 없는 정수로 만듭니다. */
        void setUint( uint64 value ) const;
        /** @brief 실수로 만듭니다. */
        void setFloat( float64 value ) const;
        /** @brief 문자열로 만듭니다. */
        void setString( string_view value ) const;
        /** @brief 빈 객체로 만듭니다. */
        void setObject() const;
        /** @brief 빈 배열로 만듭니다. */
        void setArray() const;

        /**
         * @brief 객체 멤버를 만들거나 기존 키를 반환합니다.
         * @details bIgnoreCaseKeys 가 true 이면 대소문자만 다른 기존 키를 재사용합니다.
         */
        JSONValue set( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 배열 끝에 null 원소를 붙이고 반환합니다. */
        JSONValue pushBack() const;

    private:
        /** @brief 멤버 하나를 읽은 결과입니다. 타입이 다르면 읽는 함수가 키와 함께 오류로 남깁니다. */
        enum class JSONValueRead : uint8
        {
            Read,
            Absent,
            Invalid,
        };
        JSONValueRead parseMember( string_view key, int32& outValue ) const;
        JSONValueRead parseMember( string_view key, uint32& outValue ) const;
        JSONValueRead parseMember( string_view key, int64& outValue ) const;
        JSONValueRead parseMember( string_view key, uint64& outValue ) const;
        JSONValueRead parseMember( string_view key, float32& outValue ) const;
        JSONValueRead parseMember( string_view key, float64& outValue ) const;
        JSONValueRead parseMember( string_view key, bool& outValue ) const;
        JSONValueRead parseMember( string_view key, string& outValue ) const;
        /** @brief 필수 멤버가 없다고 오류로 남깁니다. */
        void reportMissing( string_view key ) const;

        friend class JSONDocument;
        /** @brief 내부 JSON 노드 포인터로 핸들을 만듭니다. */
        explicit JSONValue( void* pValue )
            : _pValue{ pValue } {}

        void* _pValue{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `parent` 안의 이름 붙은 배열을 돌며 **객체 원소만** 넘겨줍니다.
     * @param pArrayName 배열 멤버 이름("nodes", "links" 등). 없거나 배열이 아니면 아무것도 하지 않습니다.
     * @param visit `(const JSONValue& element, size_t index)` 를 받는 호출 가능 객체입니다.
     * @details 손으로 읽는 JSON 에셋이 함께 쓴다. 배열을 얻고, 배열인지 묻고, 개수를 세고, 원소가 객체가 아니면
     *          건너뛰는 일을 한 곳에 모았다. `isObject` 검사를 빠뜨리면 **망가진 파일 하나로 그 에셋이 통째로 깨지므로**, 검사를
     *          여기 한 번만 둔다.
     * @note 인덱스를 함께 넘기는 이유는 id 가 없는 원소에 **순번을 기본값으로** 쓰는 에셋이 있기 때문입니다.
     */
    template <typename VisitFn>
    void forEachObjectInArray( const JSONValue& parent, const utf8* pArrayName, VisitFn&& visit )
    {
        const JSONValue arrayValue = parent.get( pArrayName );
        if ( arrayValue.isArray() == false )
            return;

        const size_t count = arrayValue.size();
        for ( size_t index = 0; index < count; ++index )
        {
            const JSONValue element = arrayValue.at( index );
            if ( element.isObject() )
                visit( element, index );
        }
    }

    /**
     * @class JSONDocument
     * @brief JSON 트리입니다. TypeInfo 없이 손으로 읽을 때 씁니다.
     */
    class SW_API JSONDocument
    {
    public:
        // ------------------------------------------------------------------------------
        // 3) 수명: 복사 금지, 이동 가능
        // ------------------------------------------------------------------------------
        /** @brief 빈(null) 문서를 만듭니다. */
        JSONDocument();
        /** @brief 파싱 트리를 해제합니다. */
        ~JSONDocument();

        /** @brief 복사를 금지합니다. */
        JSONDocument( const JSONDocument& ) = delete;
        /** @brief 대입을 금지합니다. */
        JSONDocument& operator=( const JSONDocument& ) = delete;
        /** @brief 문서를 이동합니다. */
        JSONDocument( JSONDocument&& ) noexcept;
        /** @brief 문서를 이동 대입합니다. */
        JSONDocument& operator=( JSONDocument&& ) noexcept;

        /** @brief 문서를 null 로 비웁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 4) 파싱과 로드
        // ------------------------------------------------------------------------------
        /**
         * @brief JSON 전체 문서를 파싱합니다.
         * @param sourceName 오류에 적는 이름(대개 경로)입니다. 비우면 `<memory>` 입니다.
         */
        [[nodiscard]] bool parse( string_view jsonText, string_view sourceName = {} );
        /**
         * @brief JSON 인지 **알아보려고** 파싱합니다. `parse` 와 같지만 실패를 로그로 남기지 않습니다(오류 텍스트는 비어 있습니다).
         * @details 값이 JSON 스칼라인지 확인하는 쪽(`JSONSerializer` 쓰기)이 `parse` 를 쓰면, `"0,0,0"` 같은 정상 값을 쓸 때마다 `[Error]` 가 찍힌다.
         */
        [[nodiscard]] bool tryParse( string_view jsonText );

        /** @brief 절대 경로를 읽고 파싱합니다. */
        [[nodiscard]] bool loadFile( string_view absPath );

        /** @brief 리소스 상대 경로를 해석한 뒤 읽고 파싱합니다. */
        [[nodiscard]] bool loadResource( string_view relativePath, string* pOutAbsPath = nullptr );

        /**
         * @brief 절대 경로나 작업 경로에 파일이 있으면 loadFile 로, 없으면 loadResource 로 읽습니다.
         * @details 에셋 상대 경로와 에디터 절대 경로를 한 번의 호출로 처리한다.
         */
        [[nodiscard]] bool loadPath( string_view path, string* pOutAbsPath = nullptr );

        /**
         * @brief 마지막 parse 나 load 가 실패한 이유입니다. 성공했으면 빈 문자열입니다. `XMLDocument::getLastError` 와 같은 형태입니다.
         * @details 구문 오류는 `경로:줄:열: 이유` 형태다. 어느 파일의 어디인지 알 수 있다.
         */
        const string& getLastError() const { return _lastError; }

        /** @brief 루트 값을 반환합니다. */
        JSONValue getRoot() const;

        // ------------------------------------------------------------------------------
        // 5) 쓰기
        // ------------------------------------------------------------------------------
        /** @brief 루트를 빈 객체로 만들고 반환합니다. */
        JSONValue makeObject();
        /** @brief 루트를 빈 배열로 만들고 반환합니다. */
        JSONValue makeArray();
        /** @brief 현재 문서를 JSON 문자열로 직렬화합니다. indent 가 음수이면 한 줄로 만듭니다. */
        string dump( int32 indent = -1 ) const;
        /** @brief 현재 문서를 절대 경로에 씁니다. indent 가 음수이면 한 줄로 씁니다. */
        [[nodiscard]] bool saveFile( string_view absPath, int32 indent = -1 ) const;

        // ------------------------------------------------------------------------------
        // 6) 문자열 유틸: 따옴표 안의 이스케이프
        // ------------------------------------------------------------------------------
        /** @brief JSON 따옴표 값 안에 넣을 문자열을 이스케이프합니다. */
        static string escapeString( string_view value );
        /** @brief JSON 문자열 값의 이스케이프를 해제합니다(주변 따옴표는 제외합니다). */
        static string unescapeString( string_view value );
        /**
         * @brief 최상위 객체에서 필드를 문자열로 추출합니다.
         * @details 문자열이면 내용을, 그 밖의 스칼라는 dump 한 값을 반환합니다. 객체나 배열이면 빈 문자열입니다.
         */
        static string extractStringField( string_view json, string_view fieldName,
                                          bool bIgnoreCaseKeys = true );

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
        string           _lastError; /**< 마지막 parse 나 load 의 실패 이유(`getLastError`) */
    };
} // namespace sw
