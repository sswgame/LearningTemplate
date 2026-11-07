/**
 * @file ResourceComposer.h
 * @brief 리소스 글을 읽기 전에 합치는 한 길입니다. 모든 읽는 쪽(`XMLDocument::parseData` · `ReflectedXMLFile` · `XMLSerializer::loadFromAsset` · `Material` · 프리팹 머리 · 팩 쿠커)이 이 함수를 지납니다.
 * @details 단계는 부모(`_base`) → 조건(`_when`) → 덮어쓰기 층입니다(`docs/ResourceStructure.md`). 읽는 쪽은 단계를 모른다 — 펼친 글만 받는다.
 *          언리얼은 Device Profile · 팩 우선순위 · CDO 가 저마다의 길로 값을 얹고, Godot 은 기능 태그와 씬 상속이 따로다(기억에 의존). 여기는 차이를 얹는 일이 글 수준에서 한 함수를 지나므로
 *          읽는 코드가 변형을 모르고, 쿠킹본과 런타임이 같은 결과를 본다.
 *
 *          - **조건(`<_when _if="식" 칸="값"/>`)**: 원소 안에 적어 그 원소의 속성을 바꾸거나(`칸="값"`) 그 원소를 뺀다(`_op="Remove"`). 한 원소의 조건은 적힌 차례로 얹는다.
 *            조건이 고치는 것은 자기를 품은 원소 하나뿐이라 합치기 규칙(열쇠 · 스키마)이 필요 없다 — 쿠커(규칙 표) · 개발 실행(리플렉션) · 배포 런타임(규칙 표가 없다)이 같은 답을 낸다.
 *            조건부로 더하는 원소는 "원소를 적고 반대 조건으로 뺀다" 로 쓴다. 팩 쿠킹은 쿠킹 축 조건만 풀고(`ResourceTagTiming::Cook`), 한 원소에서 로드 축 조건이 나온 뒤의 조건은
 *            차례를 지키려 함께 남긴다. 런타임은 남은 조건을 모두 푼다(쿠킹 축 값은 런타임도 같은 값을 든다 — 엔진 기동이 넣는다).
 *          - **덮어쓰기 층(`ResourceLayers`)**: 리소스 id 로 읽을 때(`ResourceContext::withAsset`)만 얹는다. 층 파일 `<뿌리>/<id>` 는 원본을 암묵적 부모로 가진 차이이고,
 *            합치기 규칙은 읽는 쪽의 형식에서 온다(리플렉션 · 손 규칙 · 합치기 함수 · 카탈로그 표). 팩 쿠킹은 층을 펼치지 않는다 — 공용 도메인 팩(`common` · `engine`)은 게임마다 같아야 한다.
 *
 *          규칙의 출처는 문서 루트 이름입니다: 카탈로그 표 · 문서 표(`Config/Dev/DataCatalogInheritance.json`) → 합치기 함수(`SW_DATA_INHERITANCE_MERGE`) → 손 형식(`SW_DATA_INHERITANCE_SCHEMA`). 읽기는 리플렉션 형식에서도 규칙을 만들지만 쿠킹은 그러지 않습니다. 어느 규칙이든 결과는 부모 없는 글입니다.
 *          **팩 쿠킹은 게임 모듈을 올리기 전에 돕니다**(`HeadlessSubsystem::runCookTask`). 쿠커는 대상마다 지어진 형식도 다릅니다(서버는 Renderer 가 없다). 그래서 리플렉션 형식이 `_base` 를 쓰면 문서 표에 한 줄을 적습니다(Engine 형식도).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    enum class ResourceTagTiming : uint8;

    class ResourceTagSet;

    struct TypeInfo;

    /**
     * @struct ResourceContext
     * @brief 합치는 문맥입니다. 읽는 쪽이 자기 자리를 고르는 값입니다. 조건 단계의 태그는 프로세스 값(`ResourceTags`)이라 여기 들지 않습니다.
     */
    struct SW_API ResourceContext
    {
        /** @brief 부모 단계가 규칙을 어디서 찾는가입니다. */
        enum class ParentRule : uint8
        {
            CatalogTable, ///< 카탈로그 표에 있는 루트만 펼친다. 나머지 `_base` 는 읽는 쪽 몫이다(`XMLDocument::parseData`)
            KnownSchema,  ///< 읽는 쪽이 아는 손 규칙으로 펼친다
            KnownType,    ///< 읽는 쪽이 아는 리플렉션 형식으로 펼친다
            KnownMerge,   ///< 읽는 쪽이 아는 합치기 함수로 펼친다(칸 규칙으로 풀리지 않는 형식)
            RootName,     ///< 루트 이름으로 규칙을 찾아 펼친다(팩 쿠커)
        };

        ParentRule                          _parentRule;
        DataInheritance::MakeSchemaFunction _pfnMakeSchema; ///< `KnownSchema` 의 규칙
        const TypeInfo*                     _pTypeInfo;     ///< `KnownType` 의 형식
        DataInheritance::MergeFunction      _pfnMerge;      ///< `KnownMerge` 의 함수
        IDataInheritanceSource*             _pSource;       ///< `RootName` 이 부모를 읽는 곳. 나머지는 리소스다
        AssetID                             _assetID;       ///< 리소스 id 로 읽을 때의 id(`withAsset`). 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조가 쓴다. 비면 글 읽기다

        /** @brief 이 문맥으로 리소스 @p assetID 를 읽습니다 — 덮어쓰기 층을 얹고, 로드 축 조건에 기댄 결과를 기록합니다(`ResourceComposition`). 팩 쿠킹 문맥에는 쓰지 않습니다. */
        [[nodiscard]] ResourceContext withAsset( const AssetID& assetID ) const;

        /** @brief 범용 데이터 읽기입니다(`XMLDocument::parseData`). 프리팹처럼 자기 입구가 부모를 합치는 형식도 머리는 이 길을 지납니다. */
        [[nodiscard]] static ResourceContext forDocument();
        /** @brief 손으로 읽는 XML 형식의 읽기입니다. */
        [[nodiscard]] static ResourceContext forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema );
        /** @brief 리플렉션 형식의 읽기입니다. */
        [[nodiscard]] static ResourceContext forType( const TypeInfo& typeInfo );
        /** @brief 칸 규칙으로 풀리지 않는 형식의 읽기입니다(`SW_DATA_INHERITANCE_MERGE`). */
        [[nodiscard]] static ResourceContext forMerge( DataInheritance::MergeFunction pfnMerge );
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

        /**
         * @brief 조건 단계 하나입니다(`compose` 가 부른다). @p tags 로 `_when` 을 풀어 @p outText 에 쓰고, 바뀐 것이 있으면 @p outbChanged 가 true 입니다.
         * @param resolvedTiming `Cook` 이면 쿠킹 축 조건만 풀고 나머지는 남깁니다(팩 쿠킹). `Load` 면 모두 풉니다.
         * @param inoutListLoadTag 푼 조건과 남긴 조건이 묻는 로드 축 태그를 더합니다(겹치지 않게).
         * @return 조건 원소가 틀렸으면(식 · 모르는 축 · 자식 원소 · 루트 빼기) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool resolveConditions( string_view text, string_view id, ResourceTagTiming resolvedTiming, const ResourceTagSet& tags, string& outText, bool& outbChanged,
                                                     vector<string>& inoutListLoadTag );
    };
} // namespace sw
