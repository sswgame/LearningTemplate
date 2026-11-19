/**
 * @file CatalogKey.h
 * @brief 카탈로그 항목 하나를 가리키는 id 칸(`CatalogKey<Tag>`)입니다 — 다른 카탈로그의 키를 넣으면 컴파일 오류입니다.
 * @details 컴포넌트 · 설정 구조체가 카탈로그 항목을 `hashed_string` · `string` 으로 가리키면, 어빌리티 세트 칸에 유닛 id 를 넣어도 컴파일되고 쿠킹도 통과해
 *          실행해야 "없는 id" 로그가 난다. 이 형식은 항목 종류(`Tag` — 보통 그 카탈로그의 정의 구조체 `AbilitySetDef`)를 형식에 담는다.
 *          `CatalogKey<A>` 와 `CatalogKey<B>` 사이에는 변환이 없고, 글 · `hashed_string` 에서 만들 때는 `explicit` 이다.
 *          값은 `hashed_string` 하나라 칸 비용이 같다. 언리얼 `FDataTableRowHandle` 의 행 이름 · ezEngine 의 형식 있는 에셋 · 데이터 참조와 같은 자리다.
 *
 *          직렬화는 이 형식 하나만 안다 — 파서가 `CatalogKey<T>` 칸의 형식을 "CatalogKey" 로 모으고(`AssetRef<T>` 와 같은 길) 항목 종류 이름(`T`)을
 *          프로퍼티 메타 `_catalogKeyTag` 에 적는다. 저장 꼴은 id 글 그대로다(`_abilitySetID="ZoneWalker"`).
 *
 *          항목 종류가 어느 카탈로그 루트 · 원소를 가리키는지는 데이터 표(`DataCatalogReferenceRules` — `<CatalogKey tag catalog entry>`)에 적는다.
 *          팩 쿠커는 게임 모듈을 올리기 전에 돌아 리플렉션을 보지 못하기 때문이다. 표의 `<Field owner field tag>` 줄이 쿠커에게 칸을 알리고,
 *          쿠커는 씬 · 프리팹의 그 칸 값이 쿠킹하는 팩들 안의 그 카탈로그 항목인지 대조해 없으면 쿠킹 실패다. 표 줄과 코드가 어긋나면
 *          `DataCatalogReferencesTest.FieldTableMatchesReflection` 이 진다. 인스펙터는 같은 표로 항목을 모아 콤보로 고르게 한다.
 *
 *          쓰는 법:
 *            PROPERTY()
 *            CatalogKey<AbilitySetDef> _abilitySetID;            // 칸
 *            const AbilitySetDef* pSet = catalog.find( _abilitySetID ); // 형식 있는 조회(`GameCatalog::find`)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class SerializeContext;

    /**
     * @class CatalogKeyBase
     * @brief `CatalogKey<Tag>` 의 값 부분입니다(항목 id). 직접 만들지 않고 `CatalogKey<Tag>` 로 씁니다. 직렬화 처리기("CatalogKey")가 이 형식으로 읽고 쓴다.
     */
    class SW_API CatalogKeyBase
    {
    public:
        /** @brief 리플렉션 형식 이름입니다. 파서가 `CatalogKey<T>` 칸의 형식을 이 이름으로 적는다(`PropertyResolver::resolveCatalogKeyProperty`). */
        static constexpr const utf8* kTypeName = "CatalogKey";

        /** @brief 항목 id 입니다. */
        [[nodiscard]] const hashed_string& key() const { return _key; }
        /** @brief 비었으면 true 입니다. 빈 키는 "가리키지 않음" 이다(필수 칸은 `PROPERTY( Required )` 가 본다). */
        [[nodiscard]] bool empty() const { return _key.empty(); }
        /** @brief id 글(끝에 0)입니다. 빈 키면 빈 글이다. */
        [[nodiscard]] const utf8* c_str() const { return _key.c_str(); }

        /** @brief 저장 글(id 그대로)에서 키를 넣습니다. 앞뒤 공백 · 따옴표는 뺀다. 빈 글은 키를 비운다. */
        void assignText( string_view text );
        /** @brief 저장 글입니다(id 그대로). */
        [[nodiscard]] string toText() const { return string( _key.view() ); }

        /** @brief 리플렉션 글 처리기("CatalogKey")를 직렬화 문맥에 등록합니다. 기본 문맥이 만들어질 때 한 번 부른다. */
        static void registerSerializeHandler( SerializeContext& context );

    protected:
        CatalogKeyBase()
            : _key{}
        {
        }
        explicit CatalogKeyBase( const hashed_string& key )
            : _key{ key }
        {
        }

    private:
        hashed_string _key;
    };

    /**
     * @class CatalogKey
     * @brief 항목 종류 @p TTag 의 카탈로그 항목 id 입니다. 다른 종류의 키 · 글 · `hashed_string` 에서 암묵 변환이 없다.
     * @tparam TTag 항목 종류입니다. 보통 그 카탈로그의 정의 구조체(`AbilitySetDef`)이고, 그 이름이 참조 표의 `<CatalogKey tag>` 다.
     */
    template <typename TTag>
    class CatalogKey : public CatalogKeyBase
    {
    public:
        using Tag = TTag;

        CatalogKey() = default;
        /** @brief 코드가 id 를 정해 만들 때 씁니다(테스트 · 다른 데이터에서 읽은 id). 데이터 칸은 리플렉션 읽기가 채운다. */
        explicit CatalogKey( const hashed_string& key )
            : CatalogKeyBase{ key }
        {
        }

        [[nodiscard]] bool operator==( const CatalogKey& other ) const { return key() == other.key(); }
        [[nodiscard]] bool operator!=( const CatalogKey& other ) const { return key() != other.key(); }
    };
} // namespace sw
