/**
 * @file JsonDocument.h
 * @brief 리플렉션 없는 JSON 파싱·탐색 (콘텐츠 테이블, 맵, 툴)
 * @note 리플렉션 객체 그래프는 JsonSerializer를 사용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @enum JsonType
     * @brief JSON 값의 종류
     */
    enum class JsonType : uint8
    {
        Null = 0,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    /**
     * @class JsonValue
     * @brief JsonDocument 안의 경량 핸들 (clear/destroy 이후 무효)
     */
    class SW_API JsonValue
    {
    public:
        /** @brief 빈(무효) 값 핸들. */
        JsonValue() = default;

        /** @brief 값이 유효하면 true. */
        bool isValid() const { return _pValue != nullptr; }
        /** @brief isValid()와 동일. */
        explicit operator bool() const { return isValid(); }

        // ------------------------------------------------------------------------------
        // 1) 읽기 — 타입, 스칼라, 객체/배열
        // ------------------------------------------------------------------------------
        /** @brief JSON 타입을 반환합니다. 무효면 Null. */
        JsonType type() const;
        /** @brief 객체이면 true. */
        bool isObject() const { return type() == JsonType::Object; }
        /** @brief 배열이면 true. */
        bool isArray() const { return type() == JsonType::Array; }
        /** @brief 문자열이면 true. */
        bool isString() const { return type() == JsonType::String; }
        /** @brief 숫자이면 true. */
        bool isNumber() const { return type() == JsonType::Number; }
        /** @brief 불리언이면 true. */
        bool isBool() const { return type() == JsonType::Bool; }
        /** @brief null이면 true. */
        bool isNull() const { return type() == JsonType::Null; }

        /** @brief 문자열 내용. 문자열이 아니면 dump한 스칼라(따옴표 없음). */
        string asString() const;
        /** @brief 정수로 읽습니다. */
        int64 asInt( int64 fallback = 0 ) const;
        /** @brief 부호 없는 정수로 읽습니다. */
        uint64 asUint( uint64 fallback = 0 ) const;
        /** @brief 실수로 읽습니다. */
        float64 asFloat( float64 fallback = 0.0 ) const;
        /** @brief 불리언으로 읽습니다. */
        bool asBool( bool fallback = false ) const;

        /** @brief 객체 멤버 또는 배열 원소 개수. */
        size_t size() const;
        /** @brief 객체 멤버 이름을 삽입 순으로 반환합니다. */
        vector<string> memberNames() const;
        /** @brief 객체 멤버를 찾습니다. 없으면 무효 핸들. */
        JsonValue get( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 객체에 키가 있으면 true. */
        bool has( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 배열 원소. 범위 밖이면 무효 핸들. */
        JsonValue at( size_t index ) const;

        /** @brief 이 서브트리를 JSON 문자열로 직렬화합니다. indent<0 이면 한 줄. */
        string dump( int32 indent = -1 ) const;
        /** @brief 다른 핸들의 값을 이 노드에 복사합니다. */
        void assignFrom( const JsonValue& other ) const;

        // ------------------------------------------------------------------------------
        // 2) 쓰기 — 문서는 호출자가 소유
        // ------------------------------------------------------------------------------
        /** @brief null로 만듭니다. */
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
         * @details bIgnoreCaseKeys이면 대소문자만 다른 기존 키를 재사용합니다.
         */
        JsonValue set( string_view key, bool bIgnoreCaseKeys = true ) const;
        /** @brief 배열 끝에 null 원소를 붙이고 반환합니다. */
        JsonValue pushBack() const;

    private:
        friend class JsonDocument;
        /** @brief 내부 JSON 노드 포인터로 핸들을 만듭니다. */
        explicit JsonValue( void* pValue )
            : _pValue{ pValue } {}

        void* _pValue{ nullptr };
    };

    /**
     * @class JsonDocument
     * @brief JSON 트리. TypeInfo 없는 수동 로드용
     */
    class SW_API JsonDocument
    {
    public:
        // ------------------------------------------------------------------------------
        // 3) 수명 — 복사 금지, 이동 가능
        // ------------------------------------------------------------------------------
        /** @brief 빈(null) 문서를 만듭니다. */
        JsonDocument();
        /** @brief 파스 트리를 해제합니다. */
        ~JsonDocument();

        /** @brief 복사를 금지합니다. */
        JsonDocument( const JsonDocument& ) = delete;
        /** @brief 대입을 금지합니다. */
        JsonDocument& operator=( const JsonDocument& ) = delete;
        /** @brief 문서를 이동합니다. */
        JsonDocument( JsonDocument&& ) noexcept;
        /** @brief 문서를 이동 대입합니다. */
        JsonDocument& operator=( JsonDocument&& ) noexcept;

        /** @brief 문서를 null로 비웁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 4) 파싱 · 로드
        // ------------------------------------------------------------------------------
        /** @brief JSON 전체 문서를 파싱합니다. */
        bool parse( string_view jsonText );

        /** @brief 절대 경로를 읽고 파싱합니다. */
        bool loadFile( string_view absPath );

        /** @brief 리소스 상대 경로를 해석한 뒤 읽고 파싱합니다. */
        bool loadResource( string_view relativePath, string* pOutAbsPath = nullptr );

        /**
         * @brief 절대/작업 경로가 있으면 loadFile, 없으면 loadResource.
         * @details 에셋 상대 경로와 에디터 절대 경로를 한 호출로 처리합니다.
         */
        bool loadPath( string_view path, string* pOutAbsPath = nullptr );

        /** @brief 루트 값. */
        JsonValue root() const;

        // ------------------------------------------------------------------------------
        // 5) 쓰기
        // ------------------------------------------------------------------------------
        /** @brief 루트를 빈 객체로 만들고 반환합니다. */
        JsonValue makeObject();
        /** @brief 루트를 빈 배열로 만들고 반환합니다. */
        JsonValue makeArray();
        /** @brief 현재 문서를 JSON 문자열로 직렬화합니다. indent<0 이면 한 줄. */
        string dump( int32 indent = -1 ) const;
        /** @brief 현재 문서를 절대 경로에 씁니다. indent<0 이면 한 줄. */
        bool saveFile( string_view absPath, int32 indent = -1 ) const;

        // ------------------------------------------------------------------------------
        // 6) 문자열 유틸 — 따옴표 안의 이스케이프
        // ------------------------------------------------------------------------------
        /** @brief JSON 따옴표 값 안에 넣을 문자열을 이스케이프합니다. */
        static string escapeString( string_view value );
        /** @brief JSON 문자열 값의 이스케이프를 해제합니다 (주변 따옴표 제외). */
        static string unescapeString( string_view value );
        /**
         * @brief 최상위 객체에서 필드를 문자열로 추출합니다.
         * @details 문자열이면 내용, 그 외 스칼라는 dump. 객체/배열은 empty.
         */
        static string extractStringField( string_view json, string_view fieldName,
                                          bool bIgnoreCaseKeys = true );

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw
