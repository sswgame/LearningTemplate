/**
 * @file TypeNameMap.h
 * @brief ReflectionParser용 clang 표기 → canonical 맵 (ReflectBuiltins에서 채움).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 내장 타입의 메모리 모양입니다 — `ReflectBuiltins.xxx` 의 Layout 칸(엔진 `ReflectBuiltinLayout` 과 같은 철자 집합). */
    enum class BuiltinLayoutKind : uint8
    {
        None,    ///< none
        Number,  ///< num — 산술 스칼라 하나
        Float32, ///< f32(멤버, …)
        Int32,   ///< i32(멤버, …)
        Angle,   ///< angle — 라디안 float32 하나
    };

    /** @brief `ReflectBuiltins.xxx` 의 TYPE 줄 하나가 파서에 알리는 모양 · 역할입니다. 손 표(고정 배치 수학 표 · Interp 허용 목록)를 대신하는 정본입니다. */
    struct BuiltinTypeTraits
    {
        vector<string>         _listMember; ///< f32 · i32 의 멤버 이름(선언 순서). 고정 배치형 코덱이 `offsetof` 로 쓴다
        BuiltinLayoutKind      _layout;
        uint8                  _bPacked  : 1; ///< 글 처리기가 `packed` 다
        uint8                  _bInterp  : 1; ///< Roles 에 Interp — `PROPERTY( Interp )` 를 받는다
        uint8                  _bState   : 1; ///< Roles 에 State — `PROPERTY( RuntimeState )` 칸이 될 수 있다
        uint8                  _bLocal   : 1; ///< Roles 에 Local — 프로세스 안에서만 뜻이 있는 값이라 `PROPERTY( SaveGame )` 을 받지 않는다
        [[maybe_unused]] uint8 _reserved : 4;

        BuiltinTypeTraits()
            : _listMember{}
            , _layout{ BuiltinLayoutKind::None }
            , _bPacked{ SW_FALSE }
            , _bInterp{ SW_FALSE }
            , _bState{ SW_FALSE }
            , _bLocal{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    // ------------------------------------------------------------------------------
    // 1) maps — clang 표기 alias → canonical
    // ------------------------------------------------------------------------------
    class TypeNameMap
    {
    public:
        TypeNameMap();
        ~TypeNameMap() = default;

        /** @brief builtins 등록이 완료되었는지 반환합니다. */
        bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

        /** @brief clang 수식어를 제거한 뒤 alias → canonical 로 정규화합니다. */
        string normalize( const string& clangSpelling ) const;

        /** @brief 이 표기(수식어를 떼고, `sw::` 를 떼고)가 표에 있는 이름인지 — 내장 스칼라 · 문자열 · 수학 타입(`int32` · `string` · `float3` …). */
        bool isKnown( const string& clangSpelling ) const;

        /** @brief canonical·네임스페이스·별칭을 맵에 등록합니다. */
        void registerEntry( const string& canonical, const string& nameSpace,
                            const vector<string>& aliases );

        /** @brief 정규 이름 @p canonical 의 모양 · 역할을 적습니다(`ReflectBuiltinsLoader` 가 줄마다 한 번). */
        void registerTraits( const string& canonical, const BuiltinTypeTraits& traits );
        /** @brief 정규 이름 @p canonical 의 모양 · 역할입니다. 내장 타입이 아니면(열거형 · 구조체) nullptr 입니다. */
        const BuiltinTypeTraits* findTraits( const string& canonical ) const;
        /** @brief 정규 이름 @p canonical 이 압축 타입(글 처리기 `packed`)인지 반환합니다. 고정 배치형 코덱이 이 타입을 받지 못하는 것을 오류로 알리는 데 씁니다. */
        bool isPacked( const string& canonical ) const;

        /** @brief 정규화 전에 떼어 낼 clang 수식어(`const ` · `class ` …)를 정합니다. parser_config 의 `type_strip_prefixes` 입니다. */
        void setStripPrefixes( const vector<string>& listPrefix ) { _listStripPrefix = listPrefix; }

        /** @brief 등록 항목을 비웁니다. */
        void clear();
        /** @brief 로드 완료 플래그를 설정합니다. */
        void setLoaded( bool bLoaded ) { _bLoaded = bLoaded ? SW_TRUE : SW_FALSE; }

    private:
        /** @brief alias 키를 canonical 에 연결합니다. */
        void addKey( const string& key, const string& canonical );

        /** @brief clang 수식어(const/class 등)와 참조를 제거합니다. */
        string_view stripDecorations( string_view spelling ) const;

    private:
        unordered_map<string, string>            _mapAliasToCanonical;
        vector<string>                           _listStripPrefix;
        unordered_map<string, BuiltinTypeTraits> _mapTraits;
        uint8                                    _bLoaded  : 1;
        [[maybe_unused]] uint8                   _reserved : 7;
    };

} // namespace sw
