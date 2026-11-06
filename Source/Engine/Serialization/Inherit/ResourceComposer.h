/**
 * @file ResourceComposer.h
 * @brief 리소스 글을 읽기 전에 합치는 한 길입니다. 모든 읽는 쪽(`XMLDocument::parseData` · `ReflectedXMLFile` · `XMLSerializer::loadFile` · `Material` · 프리팹 머리 · 팩 쿠커)이 이 함수를 지납니다.
 * @details 단계는 부모(`_base`) → 조건(`_when`) → 덮어쓰기 층입니다(`docs/ResourceStructure.md`). 지금은 부모 단계만 있고, 뒤의 두 단계는 `compose` 안 부모 단계 뒤에 더합니다 — 읽는 쪽은 고치지 않습니다.
 *          언리얼은 Device Profile · 팩 우선순위 · CDO 가 저마다의 길로 값을 얹고, Godot 은 기능 태그와 씬 상속이 따로다(기억에 의존). 여기는 차이를 얹는 일이 글 수준에서 한 함수를 지나므로
 *          읽는 코드가 변형을 모르고, 쿠킹본과 런타임이 같은 결과를 본다.
 *
 *          규칙의 출처는 문서 루트 이름입니다: 카탈로그 표 · 문서 표(`Config/Dev/DataCatalogInheritance.json`) → 손 형식(`SW_DATA_INHERITANCE_SCHEMA`) → 리플렉션 형식.
 *          **팩 쿠킹은 게임 모듈을 올리기 전에 돕니다**(`HeadlessSubsystem::runCookTask`). 정적 등록과 리플렉션은 Engine 것만 보이므로 게임 · 키트 형식이 `_base` 를 쓰면 문서 표에 한 줄을 적습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @struct ResourceContext
     * @brief 합치는 문맥입니다. 읽는 쪽이 자기 자리를 고르는 값이고, 조건 단계의 태그 집합도 여기에 더합니다.
     */
    struct SW_API ResourceContext
    {
        /** @brief 부모 단계가 규칙을 어디서 찾는가입니다. */
        enum class ParentRule : uint8
        {
            CatalogTable, ///< 카탈로그 표에 있는 루트만 펼친다. 나머지 `_base` 는 읽는 쪽 몫이다(`XMLDocument::parseData`)
            KnownSchema,  ///< 읽는 쪽이 아는 손 규칙으로 펼친다
            KnownType,    ///< 읽는 쪽이 아는 리플렉션 형식으로 펼친다
            RootName,     ///< 루트 이름으로 규칙을 찾아 펼친다(팩 쿠커)
        };

        ParentRule                          _parentRule;
        DataInheritance::MakeSchemaFunction _pfnMakeSchema; ///< `KnownSchema` 의 규칙
        const TypeInfo*                     _pTypeInfo;     ///< `KnownType` 의 형식
        IDataInheritanceSource*             _pSource;       ///< `RootName` 이 부모를 읽는 곳. 나머지는 리소스다

        /** @brief 범용 데이터 읽기입니다(`XMLDocument::parseData`). 프리팹처럼 자기 입구가 부모를 합치는 형식도 머리는 이 길을 지납니다. */
        [[nodiscard]] static ResourceContext forDocument();
        /** @brief 손으로 읽는 XML 형식의 읽기입니다. */
        [[nodiscard]] static ResourceContext forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema );
        /** @brief 리플렉션 형식의 읽기입니다. */
        [[nodiscard]] static ResourceContext forType( const TypeInfo& typeInfo );
        /** @brief 팩 쿠킹입니다. 부모는 @p source 로 읽고 그 읽기가 쿠킹 작업의 입력이 됩니다. */
        [[nodiscard]] static ResourceContext forPackCook( IDataInheritanceSource& source );
    };

    /**
     * @struct ResourceComposer
     * @brief 합치기의 입구입니다. 파일 머리말 참고.
     */
    struct SW_API ResourceComposer
    {
        /**
         * @brief @p text 에 얹을 차이가 있으면 합쳐 @p outText 에 쓰고 @p outbComposed 를 true 로 둡니다. 없으면 글 검색 한 번으로 끝나고 @p outbComposed 는 false 입니다(읽는 쪽은 @p text 를 그대로 읽는다).
         * @param id 오류에 적는 이름입니다(대개 리소스 id).
         * @return 합치다 실패하면 오류를 남기고 false 입니다. 부모 없이 자식 칸만으로 읽지 않습니다.
         */
        [[nodiscard]] static bool compose( string_view text, string_view id, const ResourceContext& context, string& outText, bool& outbComposed );
    };
} // namespace sw
