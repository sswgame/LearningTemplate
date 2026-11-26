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
    /** @brief 바이트를 세는 방출 함수 구역입니다. 표로 바꿀 다음 후보를 숫자로 고르는 데 쓴다. */
    enum class CodeEmitSection : uint8
    {
        FileHeader,        ///< 머리 · include
        TypeRegistrar,     ///< 타입 서술자의 골격과 타입 칸(아래 구역을 뺀 몫)
        PropertyBinding,   ///< 프로퍼티의 드문 칸 표(옛 이름 · 접근자 · 알림 · 검증 · 비트필드)
        PropertyContainer, ///< 컨테이너 모양 표(중첩 단계)
        PropertyDesc,      ///< 프로퍼티 서술자 표 · 값 검사
        PropertyMetadata,  ///< 프로퍼티 메타데이터 표
        EventList,
        MethodList,    ///< 메서드 표(호출 함수를 뺀 몫)
        MethodInvoker, ///< 메서드 호출 람다 — 형식이 있어야 해 표로 바꿀 수 없다
        LayoutSchema,  ///< 스키마 결합형 서술자 표 · 스키마
        TickBatch,
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
            constexpr const utf8* kArrName[] = { "FileHeader", "TypeRegistrar", "PropertyBinding", "PropertyContainer", "PropertyDesc", "PropertyMetadata", "EventList",
                                                 "MethodList", "MethodInvoker", "LayoutSchema", "TickBatch", "EnumRegistrar", "TypeTraits" };
            static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( CodeEmitSection::Count ), "CodeEmitSection names are out of date" );
            return kArrName[static_cast<size_t>( section )];
        }
    };

    /**
     * @class CodeEmitByteScope
     * @brief 방출 함수 하나를 감싸 그 동안 @p buffer 가 늘어난 바이트를 구역에 더합니다. 안쪽 구역의 몫은 빼므로 구역 합이 전체와 같다.
     * @details 안쪽 구역이 다른 버퍼에 쓰고 바깥이 그 글을 붙여도, 안쪽 몫을 바깥에서 빼니 두 번 세지 않는다.
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
        // 2) emit — 줄
        // ------------------------------------------------------------------------------
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

        // ------------------------------------------------------------------------------
        // 3) emit — 따옴표 / 이스케이프
        // ------------------------------------------------------------------------------
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

        // ------------------------------------------------------------------------------
        // 4) emit — C++ 식 조각(실수 리터럴 · 플래그 OR 식 · 집합체 칸 · 글 표)
        // ------------------------------------------------------------------------------
        /**
         * @brief 범위 경계 하나를 float32 글(`1.5f`)로 냅니다. 등록 단위의 허용 범위와 개발 전용 단위의 슬라이더 범위가 함께 쓴다.
         * @details 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가 생성된 파일마다 난다.
         *          경계는 float32 가 되돌아오는 자릿수(9)로 낸다. 6 자리로 자르면 `Max = 6.2831853` 이 6.283185f 가 되어 2π 를 담은 값이 범위 밖이 된다.
         */
        static string floatLiteral( const float32 value )
        {
            utf8 arrBuffer[constant::buffer::kMax64]{};
            formatstring( arrBuffer, sizeof( arrBuffer ), "%.9g", static_cast<float64>( value ) );
            StringBuilder<constant::buffer::kMax64> literal;
            literal.append( arrBuffer );
            if ( literal.view().find_first_of( ".eE" ) == string_view::npos )
                literal.append( ".0" );
            literal.append( 'f' );
            return string( literal.view() );
        }

        /** @brief 이름들로 문자열 리터럴 표 `static constexpr const ::utf8* <이름>[]` 을 씁니다(옛 이름 · 지운 이름). */
        void textTable( const string_view tableName, const vector<string>& listText )
        {
            StringBuilder<constant::buffer::kMax256> texts;
            for ( const string& text : listText )
            {
                texts.append( texts.size() > 0 ? ", " : "" );
                texts.append( quoted( text ) );
            }
            linef( "static constexpr const ::utf8* %#[] = { %# };", tableName, texts.view() );
        }

        /** @brief 플래그 이름들을 엔진 상수의 OR 식(`PropertyMetadataFlag::kReadOnly | …`)으로 만듭니다. 없으면 `0u` 입니다. */
        static string flagExpr( const vector<string>& listFlag, const utf8* pFlagStruct )
        {
            if ( listFlag.empty() )
                return string( "0u" );
            StringBuilder<constant::buffer::kMax256> b;
            for ( const string& flag : listFlag )
            {
                b.appendFormat( "%#%#::k%#", b.size() > 0 ? " | " : "", pFlagStruct, flag );
            }
            return string( b.view() );
        }

        /** @brief 집합체 칸(식, 엔진 쪽 기본값) 목록을 초기화 목록 글로 잇습니다. 뒤에서부터 기본값과 같은 칸은 뺀다 — 모두 기본값이면 빈 글입니다. */
        static string cellList( std::initializer_list<pair<string, string_view>> listCell )
        {
            size_t count = listCell.size();
            while ( count > 0 && string_view( ( listCell.begin() + ( count - 1 ) )->first ) == ( listCell.begin() + ( count - 1 ) )->second )
            {
                --count;
            }
            StringBuilder<constant::buffer::kMax512> b;
            for ( size_t index = 0; index < count; ++index )
            {
                b.append( index > 0 ? ", " : "" );
                b.append( ( listCell.begin() + index )->first );
            }
            return string( b.view() );
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
