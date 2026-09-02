/**
 * @file CodeEmit.h
 * @brief 들여쓰기를 유지하며 .gen.cpp 를 출력하는 ReflectionParser 헬퍼.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    using CodeEmitBuffer = StringBuilder<constant::kMaxBuffer8192>;

    // ------------------------------------------------------------------------------
    // 1) emit — 들여쓰기·한 줄 서식 (.gen.cpp)
    // ------------------------------------------------------------------------------
    /**
     * @class CodeEmit
     * @brief CodeEmitBuffer 위에 들여쓰기·서식을 얇게 감쌉니다. 제어 흐름은 C++에 둡니다.
     */
    class CodeEmit
    {
    public:
        /** @brief 출력 버퍼를 받아 이미터를 구성합니다. */
        explicit CodeEmit( CodeEmitBuffer& out ) noexcept
            : _out{ out }
            , _indent{ 0 }
        {
        }

        /** @brief 내부 출력 버퍼를 반환합니다. */
        CodeEmitBuffer& buffer() noexcept { return _out; }

        /** @brief 들여쓰기 단계를 올립니다. */
        CodeEmit& push( int32 levels = 1 )
        {
            _indent = static_cast<uint8>( _indent + static_cast<uint8>( levels ) );
            return *this;
        }
        /** @brief 들여쓰기 단계를 내립니다. */
        CodeEmit& pop( int32 levels = 1 )
        {
            _indent = static_cast<uint8>( ( _indent > levels ) ? ( _indent - levels ) : 0 );
            return *this;
        }
        /** @brief 현재 들여쓰기 단계를 반환합니다. */
        uint8 indentLevel() const noexcept { return _indent; }

        // ------------------------------------------------------------------------------
        // 2) emit — 원문·줄·할당·플래그
        // ------------------------------------------------------------------------------
        /** @brief 들여쓰기·개행 없이 원문을 붙입니다. */
        CodeEmit& raw( const string_view text )
        {
            _out.append( text );
            return *this;
        }

        /** @brief 빈 줄을 출력합니다. */
        CodeEmit& blank()
        {
            _out.append( '\n' );
            return *this;
        }

        /** @brief 들여쓰기 후 한 줄을 출력합니다. */
        CodeEmit& line( const string_view text )
        {
            writeIndent();
            _out.append( text );
            _out.append( '\n' );
            return *this;
        }

        /** @brief 들여쓰기 후 서식 문자열을 한 줄로 출력합니다. */
        template <typename... Args>
        CodeEmit& linef( const string_view format, const Args&... args )
        {
            writeIndent();
            _out.appendFormat( format, args... );
            _out.append( '\n' );
            return *this;
        }

        /** @brief `lhs = rhs;` 할당문을 출력합니다. */
        CodeEmit& assign( const string_view lhs, const string_view rhs )
        {
            return linef( "%# = %#;", lhs, rhs );
        }

        /** @brief cond가 참이면 `lhs = rhs;` 를 출력합니다. */
        CodeEmit& assignIf( bool cond, const string_view lhs, const string_view rhs )
        {
            if ( cond )
                assign( lhs, rhs );
            return *this;
        }

        /** @brief cond가 참이면 비트/불리언 플래그 `field = value;` 를 출력합니다. */
        CodeEmit& flagIf( bool cond, const string_view field, const string_view value = "1" )
        {
            if ( cond )
                assign( field, value );
            return *this;
        }

        /** @brief cond가 참이고 값이 비어 있지 않으면 `field = "이스케이프된문자열";` 를 출력합니다. */
        CodeEmit& assignQuotedIf( bool cond, const string_view field, const string& value )
        {
            if ( cond )
                linef( "%# = \"%#\";", field, escapeCppString( value ) );
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 3) emit — hashed_string / 따옴표 / 이스케이프
        // ------------------------------------------------------------------------------
        /** @brief `::sw::hashed_string( "name" )` 표현식을 만듭니다. 이름은 이스케이프해서 넣습니다. */
        static string hs( const string_view name )
        {
            StringBuilder<constant::kMaxBuffer1024> b;
            b.appendFormat( "::sw::hashed_string( \"%#\" )", escapeCppString( string( name ) ) );
            return string( b.view() );
        }

        /** @brief 빈 `::sw::hashed_string()` 표현식을 반환합니다. */
        static string hsEmpty() { return "::sw::hashed_string()"; }

        /** @brief 이스케이프된 C++ 문자열 리터럴 `"..."` 을 만듭니다. */
        static string quoted( const string_view value )
        {
            StringBuilder<constant::kMaxBuffer1024> b;
            b.appendFormat( "\"%#\"", escapeCppString( string( value ) ) );
            return string( b.view() );
        }

        /** @brief C++ 문자열 리터럴에 넣을 수 있도록 특수문자를 이스케이프합니다. */
        static string escapeCppString( const string& value )
        {
            string escaped;
            escaped.reserve( value.size() + 8 );
            for ( utf8 c : value )
            {
                switch ( c )
                {
                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    case '\r':
                        escaped += "\\r";
                        break;
                    case '\t':
                        escaped += "\\t";
                        break;
                    default:
                        escaped += c;
                        break;
                }
            }
            return escaped;
        }

    private:
        /** @brief 현재 들여쓰기만큼 탭을 출력합니다. */
        void writeIndent()
        {
            for ( uint8 indentIndex = 0; indentIndex < _indent; ++indentIndex )
                _out.append( '\t' );
        }

        CodeEmitBuffer& _out;
        uint8           _indent;
    };
} // namespace sw
