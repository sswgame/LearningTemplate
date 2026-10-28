/**
 * @file UIBindingValue.h
 * @brief 바인딩이 소스와 위젯 사이에 나르는 값과, 리플렉션 프로퍼티 경로를 읽고 쓰는 도우미입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /**
     * @brief 바인딩 값의 종류입니다. 종류가 같거나 바꿀 수 있으면 타입이 달라도 연결합니다(`UIBindingValueUtil::canConvert`).
     * @details 불리언과 숫자는 서로 바꾸고(0 이 아니면 true), 무엇이든 텍스트로는 바꾼다(리플렉션 텍스트 표기). 텍스트에서 숫자나 불리언으로는 바꾸지 않는다(변환기를 쓴다).
     *          그 밖의 타입(색, 벡터, 열거형, 구조체)은 같은 타입끼리, 또는 텍스트로만 연결한다.
     */
    enum class UIBindingValueKind : uint8
    {
        None,   ///< 값이 없습니다.
        Bool,   ///< bool
        Number, ///< 정수와 실수입니다.
        Text,   ///< string 과 hashed_string 입니다.
        Other   ///< 그 밖의 리플렉션 타입입니다(같은 타입끼리, 또는 텍스트로만 연결).
    };

    /**
     * @brief `UIBindingValueKind` 의 개수입니다. 종류는 맨 끝에 더하고 여기의 마지막 종류를 바꾼다.
     * @details 종류별 이름 · 경로 읽기 · 쓰기는 UIBindingValue.cpp 의 종류 표 한 줄이고, 표 크기를 이 값과 static_assert 로 맞춘다.
     *          값 변환(`toBool` · `toNumber` · `toText`)은 switch 로 두어 새 종류가 빠지면 -Werror=switch 가 알린다.
     */
    inline constexpr uint32 kUIBindingValueKindCount = static_cast<uint32>( UIBindingValueKind::Other ) + 1;
} // namespace sw

namespace sw
{
    /** @struct UIBindingValue @brief 바인딩이 나르는 값 하나입니다(종류와 그 값). */
    struct SW_API UIBindingValue
    {
        string             _text{}; ///< Text 의 값입니다(Other 를 텍스트로 바꾼 것도 여기에 담습니다).
        float64            _number{ 0.0 };
        UIBindingValueKind _kind{ UIBindingValueKind::None };
        bool               _bValue{ false };
        bool               _bInteger{ false }; ///< Number 가 정수 타입에서 왔는지 나타냅니다(형식 인자와 텍스트 표기에서 소수점을 쓰지 않습니다).

        static UIBindingValue makeBool( bool bValue );
        static UIBindingValue makeNumber( float64 number, bool bInteger );
        static UIBindingValue makeText( string_view text );

        /** @brief 불리언으로 읽습니다(숫자는 0 이 아니면 true, 텍스트는 비어 있지 않으면 true). */
        bool toBool() const;
        /** @brief 숫자로 읽습니다(불리언은 1 또는 0, 텍스트는 0). */
        float64 toNumber() const;
        /** @brief 텍스트로 씁니다(숫자는 가장 짧은 왕복 표기이고, 정수면 소수점 없이 씁니다). */
        string toText() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIPropertyPath
     * @brief 리플렉션 프로퍼티 경로(`_text`, `_slot._widthOverride`)를 해석한 결과입니다. 루트 타입부터 잎 프로퍼티까지의 연쇄입니다.
     * @details 바인딩을 연결할 때 한 번 해석하고, 값을 읽고 쓸 때는 연쇄를 따라 주소만 계산한다(이름 조회 없음).
     */
    struct SW_API UIPropertyPath
    {
        vector<const PropertyInfo*> _listProperty{}; ///< 루트 쪽부터 나열합니다(마지막이 잎).
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
    /** @brief 바인딩 값과 프로퍼티 경로 도우미입니다. */
    struct SW_API UIBindingValueUtil
    {
        /** @brief 리플렉션 타입 이름의 종류를 반환합니다. */
        static UIBindingValueKind classifyType( const hashed_string& typeName );
        /** @brief @p from 종류의 값을 @p to 종류의 필드에 쓸 수 있는지 반환합니다(같은 Other 타입끼리는 `canBindPaths` 가 따로 확인합니다). */
        static bool canConvert( UIBindingValueKind from, UIBindingValueKind to );
        /** @brief 종류 이름입니다(오류 메시지용). */
        static const utf8* getKindName( UIBindingValueKind kind );

        /**
         * @brief @p type(기반 타입 포함) 기준으로 경로 @p path 를 해석합니다. 중간은 리플렉션 구조체여야 하고 컨테이너는 받지 않습니다(잎의 `vector<string>` 은 받습니다).
         * @return 모르는 이름이면 false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool resolvePath( const TypeInfo& type, string_view path, UIPropertyPath& outPath, string& outError );

        /** @brief 경로 값을 읽습니다. Other 는 리플렉션 텍스트 표기를 Text 로 둡니다. 컨테이너는 None 입니다. */
        static UIBindingValue readValue( const UIPropertyPath& path, const void* pRoot );
        /**
         * @brief @p value 를 경로 필드의 타입으로 바꿔 씁니다. 같은 값이면 쓰지 않습니다.
         * @return 필드가 바뀌었으면 true 입니다. 바꿀 수 없으면(종류가 맞지 않거나 텍스트를 읽지 못하면) 쓰지 않고 false 입니다.
         */
        [[nodiscard]] static bool writeValue( const UIPropertyPath& path, void* pRoot, const UIBindingValue& value );
        /**
         * @brief 같은 타입의 필드끼리 값을 옮깁니다(색, 벡터, 열거형처럼 Other 종류이며, 리플렉션 텍스트 표기로 왕복합니다). 같은 값이면 쓰지 않습니다.
         * @return 필드가 바뀌었으면 true 입니다.
         */
        [[nodiscard]] static bool copyValue( const UIPropertyPath& sourcePath, const void* pSourceRoot, const UIPropertyPath& targetPath, void* pTargetRoot );
    };
} // namespace sw
