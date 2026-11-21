/**
 * @file CodeEmit.h
 * @brief 들여쓰기를 유지하며 .gen.cpp 를 출력하는 ReflectionParser 도우미입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    using CodeEmitBuffer = StringBuilder<constant::buffer::kMax8192>;

    // ------------------------------------------------------------------------------
    // 0) 방출 바이트 계측 — `.gen.cpp` 를 어느 방출 함수가 얼마나 내는가(`ReflectionParser.EmitBytes`)
    // ------------------------------------------------------------------------------
    /** @brief 바이트를 세는 방출 함수 구역입니다. 표로 바꿀 다음 후보를 숫자로 고르는 데 쓴다(docs/DesignBacklog.md 리플렉션 남은 몫 W-R3 (a)). */
    enum class CodeEmitSection : uint8
    {
        FileHeader,          ///< 머리 · 값 검사 템플릿 · include
        TypeRegistrar,       ///< 타입 등록 함수의 골격과 타입 메타데이터(아래 구역을 뺀 몫)
        PropertyStatics,     ///< 프로퍼티 별칭 · 값 검사
        NestedContainerTree, ///< 중첩 컨테이너 모양 표
        PropertyDesc,        ///< 프로퍼티 서술자 한 줄(메타데이터 채우기를 뺀 몫)
        PropertyMetadata,    ///< 프로퍼티 메타데이터 채우기 함수 본문
        EventList,
        MethodList,    ///< 메서드 표(호출 함수를 뺀 몫)
        MethodInvoker, ///< 메서드 호출 람다 — 형식이 있어야 해 표로 바꿀 수 없다
        FixedCodec,    ///< 고정 배치형 서술자 표 · 코덱
        TickBatch,
        PhysicsHook,
        EnumRegistrar,
        TypeTraits, ///< 타입 API 접근 함수
        Count,
    };

    /** @brief 구역마다 낸 바이트(안쪽 구역을 뺀 몫)입니다. 방출 하나(헤더 하나)가 한 스레드에서 채운다. */
    struct CodeEmitByteReport
    {
        array<uint64, static_cast<size_t>( CodeEmitSection::Count )> _arrBytes{};
        vector<uint64>                                               _listOpenChildBytes; ///< 열린 구역마다 그 안쪽 구역이 낸 바이트입니다(스택)

        static const utf8* toString( const CodeEmitSection section ) noexcept
        {
            constexpr const utf8* kArrName[] = { "FileHeader", "TypeRegistrar", "PropertyStatics", "NestedContainerTree", "PropertyDesc", "PropertyMetadata", "EventList",
                                                 "MethodList", "MethodInvoker", "FixedCodec", "TickBatch", "PhysicsHook", "EnumRegistrar", "TypeTraits" };
            static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( CodeEmitSection::Count ), "CodeEmitSection names are out of date" );
            return kArrName[static_cast<size_t>( section )];
        }
    };

    /**
     * @class CodeEmitByteScope
     * @brief 방출 함수 하나를 감싸 그 동안 @p buffer 가 늘어난 바이트를 구역에 더합니다. 안쪽 구역의 몫은 빼므로 구역 합이 전체와 같다.
     * @details 안쪽 구역이 다른 버퍼(메타데이터 채우기 `fillBuffer`)에 쓰고 바깥이 그 글을 붙여도, 안쪽 몫을 바깥에서 빼니 두 번 세지 않는다.
     */
    class CodeEmitByteScope
    {
    public:
        CodeEmitByteScope( CodeEmitByteReport& report, const CodeEmitBuffer& buffer, const CodeEmitSection section )
            : _report{ report }
            , _buffer{ buffer }
            , _startSize{ buffer.view().size() }
            , _section{ section }
        {
            _report._listOpenChildBytes.push_back( 0 );
        }
        ~CodeEmitByteScope()
        {
            const uint64 total = _buffer.view().size() - _startSize;
            const uint64 child = _report._listOpenChildBytes.back();
            _report._listOpenChildBytes.pop_back();
            _report._arrBytes[static_cast<size_t>( _section )] += total > child ? total - child : 0;
            if ( _report._listOpenChildBytes.empty() == false )
                _report._listOpenChildBytes.back() += total;
        }
        CodeEmitByteScope( const CodeEmitByteScope& )            = delete;
        CodeEmitByteScope& operator=( const CodeEmitByteScope& ) = delete;

    private:
        CodeEmitByteReport&   _report;
        const CodeEmitBuffer& _buffer;
        size_t                _startSize;
        CodeEmitSection       _section;
    };

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
        explicit CodeEmit( CodeEmitBuffer& outBuffer ) noexcept
            : _out{ outBuffer }
            , _indent{ 0 }
        {
        }

        /** @brief 내부 출력 버퍼를 반환합니다. */
        CodeEmitBuffer& buffer() noexcept { return _out; }

        /** @brief 지금 들여쓰기 단계입니다(다른 버퍼에 같은 깊이로 쓸 때). */
        int32 getIndent() const noexcept { return _indent; }

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

        /** @brief cond가 참이면 비트/불리언 플래그 `field = value;` 를 출력합니다. */
        CodeEmit& flagIf( bool cond, const string_view field, const string_view value = "1" )
        {
            if ( cond )
                assign( field, value );
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 3) emit — hashed_string / 따옴표 / 이스케이프
        // ------------------------------------------------------------------------------
        /** @brief `::sw::hashed_string( "name" )` 표현식을 만듭니다. 이름은 이스케이프해서 넣습니다. */
        static string hs( const string_view name )
        {
            StringBuilder<constant::buffer::kMax1024> b;
            b.appendFormat( "::sw::hashed_string( \"%#\" )", escapeCppString( name ) );
            return string( b.view() );
        }

        /** @brief 이스케이프된 C++ 문자열 리터럴 `"..."` 을 만듭니다. */
        static string quoted( const string_view value )
        {
            StringBuilder<constant::buffer::kMax1024> b;
            b.appendFormat( "\"%#\"", escapeCppString( value ) );
            return string( b.view() );
        }

        /** @brief C++ 문자열 리터럴에 넣을 수 있도록 특수문자를 이스케이프합니다. */
        static string escapeCppString( const string_view value )
        {
            StringBuilder<constant::buffer::kMax1024> escaped;
            for ( utf8 c : value )
            {
                switch ( c )
                {
                    case '\\':
                    {
                        escaped.append( "\\\\" );
                        break;
                    }
                    case '"':
                    {
                        escaped.append( "\\\"" );
                        break;
                    }
                    case '\n':
                    {
                        escaped.append( "\\n" );
                        break;
                    }
                    case '\r':
                    {
                        escaped.append( "\\r" );
                        break;
                    }
                    case '\t':
                    {
                        escaped.append( "\\t" );
                        break;
                    }
                    default:
                    {
                        escaped.append( c );
                        break;
                    }
                }
            }
            return string( escaped.view() );
        }

    private:
        /** @brief 현재 들여쓰기만큼 탭을 출력합니다. */
        void writeIndent()
        {
            for ( uint8 indentIndex = 0; indentIndex < _indent; ++indentIndex )
            {
                _out.append( '\t' );
            }
        }

    private:
        CodeEmitBuffer& _out;
        uint8           _indent;
    };
} // namespace sw
