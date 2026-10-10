/**
 * @file UIBindingValue.h
 * @brief 바인딩이 소스와 위젯 사이에 나르는 값과, 리플렉션 프로퍼티 경로를 읽고 쓰는 도우미입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /**
     * @brief 바인딩 값의 갈래입니다. 갈래가 같거나 바꿀 수 있으면 타입이 달라도 묶습니다(`UIBindingValueUtil::canConvert`).
     * @details 불리언 ↔ 숫자는 서로 바꾸고(0 아니면 참), 무엇이든 글로는 바꿉니다(리플렉션 글 표기). 글 → 숫자 · 불리언은 바꾸지 않습니다(변환기를 쓴다).
     *          그 밖의 타입(색 · 벡터 · 열거형 · 구조체)은 같은 타입끼리 · 글로만 묶습니다.
     */
    enum class UIBindingValueKind : uint8
    {
        None,   ///< 값 없음
        Bool,   ///< bool
        Number, ///< 정수 · 실수
        Text,   ///< string · hashed_string
        Other   ///< 그 밖의 리플렉션 타입(같은 타입끼리 · 글로만)
    };
} // namespace sw

namespace sw
{
    /** @struct UIBindingValue @brief 바인딩이 나르는 값 하나입니다(갈래 + 그 값). */
    struct SW_API UIBindingValue
    {
        string             _text{}; ///< Text 의 값(Other 를 글로 바꾼 것도)
        float64            _number{ 0.0 };
        UIBindingValueKind _kind{ UIBindingValueKind::None };
        bool               _bValue{ false };
        bool               _bInteger{ false }; ///< Number 가 정수 타입에서 왔다(형식 인자 · 글 표기가 소수점을 쓰지 않는다)

        static UIBindingValue makeBool( bool bValue );
        static UIBindingValue makeNumber( float64 number, bool bInteger );
        static UIBindingValue makeText( string_view text );

        /** @brief 불리언으로 읽습니다(숫자는 0 이 아니면 참, 글은 비지 않으면 참). */
        bool toBool() const;
        /** @brief 숫자로 읽습니다(불리언은 1 · 0, 글은 0). */
        float64 toNumber() const;
        /** @brief 글로 씁니다(숫자는 가장 짧은 왕복 표기, 정수면 소수점 없이). */
        string toText() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIPropertyPath
     * @brief 리플렉션 프로퍼티 경로(`_text` · `_slot._widthOverride`)를 푼 것입니다 — 뿌리 타입부터 잎 프로퍼티까지의 사슬.
     * @details 바인딩을 걸 때 한 번 풀고, 값을 읽고 쓸 때는 사슬을 따라 주소만 계산합니다(이름 조회 없음).
     */
    struct SW_API UIPropertyPath
    {
        vector<const PropertyInfo*> _listProperty{}; ///< 뿌리 쪽부터(마지막이 잎)
        UIBindingValueKind          _kind{ UIBindingValueKind::None };

        bool                isValid() const { return _listProperty.empty() == false; }
        const PropertyInfo& getRootProperty() const { return *_listProperty.front(); }
        const PropertyInfo& getLeafProperty() const { return *_listProperty.back(); }
        /** @brief 잎의 타입 이름입니다. */
        const hashed_string& getLeafTypeName() const;
        /** @brief 잎 프로퍼티를 가진 객체(사슬의 마지막 구조체)의 주소입니다. */
        void*       findLeafOwner( void* pRoot ) const;
        const void* findLeafOwner( const void* pRoot ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 바인딩 값 · 프로퍼티 경로 도우미입니다. */
    struct SW_API UIBindingValueUtil
    {
        /** @brief 리플렉션 타입 이름의 갈래입니다. */
        static UIBindingValueKind classifyType( const hashed_string& typeName );
        /** @brief @p from 갈래 값을 @p to 갈래 칸에 쓸 수 있는가입니다(같은 Other 타입끼리는 `canBindPaths` 가 따로 본다). */
        static bool canConvert( UIBindingValueKind from, UIBindingValueKind to );
        /** @brief 갈래 이름(오류 문구)입니다. */
        static const utf8* getKindName( UIBindingValueKind kind );

        /**
         * @brief @p type(기반 포함) 기준 경로 @p path 를 풉니다. 중간은 리플렉션 구조체여야 하고 컨테이너는 받지 않습니다(잎의 `vector<string>` 은 받는다).
         * @return 모르는 이름이면 false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool resolvePath( const TypeInfo& type, string_view path, UIPropertyPath& outPath, string& outError );

        /** @brief 경로 값을 읽습니다. Other 는 리플렉션 글 표기를 Text 로 둡니다. 컨테이너는 None 입니다. */
        static UIBindingValue readValue( const UIPropertyPath& path, const void* pRoot );
        /**
         * @brief @p value 를 경로 칸의 타입으로 바꿔 씁니다. 같은 값이면 쓰지 않습니다.
         * @return 칸이 바뀌었으면 true 입니다. 바꿀 수 없으면(갈래가 맞지 않거나 글을 읽지 못하면) 쓰지 않고 false 입니다.
         */
        [[nodiscard]] static bool writeValue( const UIPropertyPath& path, void* pRoot, const UIBindingValue& value );
        /**
         * @brief 같은 타입의 칸끼리 값을 옮깁니다(색 · 벡터 · 열거형처럼 Other 갈래 — 리플렉션 글 표기로 왕복). 같은 값이면 쓰지 않습니다.
         * @return 칸이 바뀌었으면 true 입니다.
         */
        [[nodiscard]] static bool copyValue( const UIPropertyPath& sourcePath, const void* pSourceRoot, const UIPropertyPath& targetPath, void* pTargetRoot );
    };
} // namespace sw
