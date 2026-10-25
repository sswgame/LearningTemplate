/**
 * @file InspectorFavorites.h
 * @brief 인스펙터 즐겨찾기 프로퍼티의 키 집합과 저장 파일 읽기 · 쓰기입니다. ImGui 에 의존하지 않으므로 테스트를 붙일 수 있습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class InspectorFavorites
     * @brief 즐겨찾기한 프로퍼티의 키(`<선언한 타입 이름>.<프로퍼티 이름>`)를 정렬된 집합으로 가집니다.
     * @details 언리얼 Details 패널의 Favorites 에 해당한다. 키의 타입은 그 프로퍼티를 **선언한** 타입이라(`makePropertyKey`), 기반 클래스의 프로퍼티를
     *          즐겨찾기하면 그 기반을 잇는 모든 타입에 함께 보인다(파생 타입마다 따로 저장하지 않는다).
     *          저장 형식은 줄마다 키 하나인 텍스트다. 에디터 상태 폴더의 `InspectorFavorites.txt` 에 둔다. 사용자 파일이므로 읽기 실패는 빈 집합이다.
     */
    class InspectorFavorites
    {
    public:
        /** @brief 타입 이름과 프로퍼티 이름으로 키를 만듭니다. 인스펙터는 선언한 타입을 고르는 `makePropertyKey` 를 씁니다. */
        static string makeKey( string_view typeName, string_view propertyName );
        /**
         * @brief @p type 에서 보이는 프로퍼티 @p prop 의 키입니다. 소유 타입은 @p type 부터 기반 쪽으로 올라가며 그 이름의 프로퍼티를 처음 선언한 타입입니다.
         * @details 파생 타입이 같은 이름을 다시 선언하면 파생 타입이 이긴다(리플렉션의 병합 규칙과 같다).
         */
        static string makePropertyKey( const TypeInfo& type, const PropertyInfo& prop );
        /** @brief @p type 이나 그 기반 타입의 키가 하나라도 있으면 true 입니다(즐겨찾기 구역에 그릴 것이 있는지). */
        bool hasAnyForTypeOrBase( const TypeInfo& type ) const;

        /** @brief 키가 집합에 있으면 true 입니다. */
        bool contains( string_view key ) const;
        /** @brief 키를 넣습니다. 새로 넣었으면 true, 이미 있거나 비었으면 false 입니다. */
        bool add( string_view key );
        /** @brief 키를 뺍니다. 있었으면 true 입니다. */
        [[nodiscard]] bool remove( string_view key );
        /** @brief 있으면 빼고 없으면 넣습니다. 바꾼 뒤 들어 있으면 true 입니다. */
        bool toggle( string_view key );
        /** @brief 이 타입 이름으로 시작하는 키가 하나라도 있으면 true 입니다. */
        bool hasAnyForType( string_view typeName ) const;
        /** @brief 키가 없으면 true 입니다. */
        bool isEmpty() const { return _listKey.empty(); }
        /** @brief 정렬된 키 목록입니다. */
        const vector<string>& getKeys() const { return _listKey; }

        /** @brief 줄마다 키 하나인 텍스트로 만듭니다. */
        string toText() const;
        /** @brief 텍스트를 읽어 집합을 바꿉니다. 빈 줄과 앞뒤 공백 · CR 은 무시하고 중복은 하나로 합칩니다. */
        void fromText( string_view text );

        /** @brief 파일을 읽습니다. 파일이 없으면 빈 집합으로 두고 false 입니다(처음 실행이면 정상). */
        [[nodiscard]] bool loadFile( string_view filePath );
        /** @brief 파일에 씁니다. 실패하면 경고하고 false 입니다. */
        [[nodiscard]] bool saveFile( string_view filePath ) const;
        /** @brief 기본 저장 파일 경로입니다(에디터 상태 폴더의 `kFileName`). */
        static string getDefaultFilePath();

        static constexpr const utf8* kFileName = "InspectorFavorites.txt";

    private:
        vector<string> _listKey; ///< 정렬되어 있고 중복이 없다
    };
} // namespace sw::editor
