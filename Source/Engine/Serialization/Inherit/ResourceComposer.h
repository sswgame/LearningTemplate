/**
 * @file ResourceComposer.h
 * @brief 리소스 글을 읽기 전에 합치는 한 길입니다. 모든 읽는 쪽(`XMLDocument::parseData` · `ReflectedXMLFile` · `XMLSerializer::loadFromAsset` · `Material` · 프리팹 머리 · 팩 쿠커)이 이 함수를 지납니다.
 * @details 단계는 부모(`_base`) → 조건(`_when`) → 덮어쓰기 층입니다(`docs/ResourceStructure.md`). 읽는 쪽은 단계를 모른다 — 펼친 글만 받는다.
 *          언리얼은 Device Profile · 팩 우선순위 · CDO 가 저마다의 길로 값을 얹고, Godot 은 기능 태그와 씬 상속이 따로다(기억에 의존). 여기는 차이를 얹는 일이 글 수준에서 한 함수를 지나므로
 *          읽는 코드가 변형을 모르고, 쿠킹본과 런타임이 같은 결과를 본다.
 *
 *          - **조건(`<_when _if="식" 칸="값"/>`)**: 원소 안에 적어 그 원소의 속성을 바꾸거나(`칸="값"`) 그 원소를 뺀다(`_op="Remove"`). 한 원소의 조건은 적힌 차례로 얹는다.
 *            조건이 고치는 것은 자기를 품은 원소 하나뿐이라 합치기 규칙(열쇠 · 스키마)이 필요 없다 — 쿠커 · 개발 실행 · 배포 런타임이 형식을 몰라도 같은 답을 낸다.
 *            조건부로 더하는 원소는 "원소를 적고 반대 조건으로 뺀다" 로 쓴다. 팩 쿠킹은 쿠킹 축 조건만 풀고(`ResourceTagTiming::Cook`), 한 원소에서 로드 축 조건이 나온 뒤의 조건은
 *            차례를 지키려 함께 남긴다. 런타임은 남은 조건을 모두 푼다(쿠킹 축 값은 런타임도 같은 값을 든다 — 엔진 기동이 넣는다).
 *          - **덮어쓰기 층(`ResourceLayers`)**: 리소스 id 로 읽을 때(`ResourceContext::withAsset`)만 얹는다. 층 파일 `<뿌리>/<id>` 는 원본을 암묵적 부모로 가진 차이이고,
 *            합치기 규칙은 읽는 쪽의 형식에서 온다(리플렉션 · 손 규칙 · 합치기 함수 · 카탈로그 표). 팩 쿠킹은 층을 펼치지 않는다 — 공용 도메인 팩(`common` · `engine`)은 게임마다 같아야 한다.
 *
 *          규칙의 출처는 문서 루트 이름입니다: 카탈로그 표 · 문서 표(`Content/engine/settings/datacataloginheritance.xml`) → 합치기 함수(`SW_DATA_INHERITANCE_MERGE`) → 손 형식(`SW_DATA_INHERITANCE_SCHEMA`). 읽기는 리플렉션 형식에서도 규칙을 만들지만 쿠킹은 그러지 않습니다. 어느 규칙이든 결과는 부모 없는 글입니다.
 *          **팩 쿠킹은 게임 모듈을 올리기 전에 돕니다**(`HeadlessSubsystem::runCookTask`). 쿠커는 대상마다 지어진 형식도 다릅니다(서버는 Renderer 가 없다). 그래서 리플렉션 형식이 `_base` 를 쓰면 문서 표에 한 줄을 적습니다(Engine 형식도).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    /** @brief 차이 글을 다시 읽어 펼친 글을 돌려줍니다. 읽지 못하면 빈 글입니다(`ResourceComposer::saveDelta`). */
    using ResourceRereadFunction = Delegate<string( const string& deltaText )>;

    enum class ResourceTagTiming : uint8;

    class ResourceTagSet;

    struct TypeInfo;

    /**
     * @struct ResourceContext
     * @brief 합치는 문맥입니다. 읽는 쪽이 자기 자리를 고르는 값입니다. 조건 단계의 태그는 엔진 값(코어 범위의 리소스 루트가 든 `ResourceTags`)이고,
     *        미리보기 문맥만 몇 축을 바꿔 끼웁니다(`withTagOverride`).
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
        const ResourceTagSet*               _pTagOverride;  ///< 엔진 태그에서 바꿔 끼울 축(`withTagOverride`, 부르는 쪽이 합치는 동안 든다). nullptr 이면 엔진 태그 그대로다

        /** @brief 이 문맥으로 리소스 @p assetID 를 읽습니다 — 덮어쓰기 층을 얹고, 로드 축 조건에 기댄 결과를 기록합니다(`ResourceComposition`). 팩 쿠킹 문맥에는 쓰지 않습니다. */
        [[nodiscard]] ResourceContext withAsset( const AssetID& assetID ) const;
        /**
         * @brief 이 문맥의 조건 단계가 엔진 태그에 @p overrideTags 의 축을 바꿔 끼워 풉니다(에디터 UI 미리보기의 기기 견본 — `screen` · `orientation`). 빈 집합이면 이 문맥 그대로입니다.
         * @details 바꿔 끼운 합성은 다시 읽기 기록(`ResourceComposition`)에 적지 않는다 — 기록은 엔진 태그로 읽은 리소스의 것이고 접속 대조 해시도 그 결과다.
         *          언리얼 위젯 디자이너의 미리보기 기기 · Godot 에디터의 기능 태그 덮기처럼 엔진 값을 건드리지 않고 한 문맥만 바꾼다(기억).
         *          @p overrideTags 는 합치기가 끝날 때까지 살아 있어야 한다(포인터로 든다). 팩 쿠킹 문맥에는 쓰지 않습니다.
         */
        [[nodiscard]] ResourceContext withTagOverride( const ResourceTagSet& overrideTags ) const;

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

        /**
         * @brief 저장할 글 @p savedText 에 원본 @p sourceText 의 조건을 되돌립니다. 편집기 사본은 지금 태그로 푼 값을 들어(형식의 칸에 조건이 들 자리가 없다) 그대로 쓰면 `_when` 이 사라지고 푼 값이 파일에 박힌다.
         * @details 언리얼이 플랫폼별 값을 편집 값과 따로 두는 것과 같은 결과를 글 수준에서 얻는다(기억에 의존). 규칙(열쇠 · 스키마)을 쓰지 않는다 — 읽기의 조건 단계와 같은 까닭이다.
         *          - 참인 조건이 정한 칸: 값이 그대로면 원본의 값을 되돌리고, 바뀌었으면 편집이 그 조건의 값이다(같은 태그로 다시 읽으면 편집한 값이다. 다른 태그의 값은 건드리지 않는다).
         *          - 그 밖의 칸: 저장할 글이 맞다. 거짓인 조건은 그대로 남는다.
         *          - 참인 조건이 뺀 원소: 사본에 없으니 원본에서 되돌린다.
         *          - 원소 짝: 같은 이름 안에서 같은 값의 속성이 가장 많은 것이다. 짝이 없는 조건 든 원소는 목록 원소면 지운 것으로 보고, 차이 저장(`_base`)이나 구조체 칸이면 남긴다(부모 · 기본값과 같아 저장이 뺀 것일 수 있다).
         *          shortcut: 참인 조건이 정한 칸을 기본값으로 되돌린 편집은 저장할 글에 칸이 없어 "그대로" 와 가릴 수 없다(조건 값이 남는다) - 형식이 푼 값과 기본값을 함께 넘기는 저장 입구가 생기면 고친다.
         * @return 원본의 조건이 틀렸거나 두 글의 루트가 다르면 오류를 남기고 false 입니다(저장하지 않는다). 원본에 조건이 없으면 @p savedText 그대로입니다.
         */
        [[nodiscard]] static bool restoreConditions( string_view savedText, string_view sourceText, string_view id, const ResourceTagSet& tags, string& outText );

        /**
         * @brief 글 리소스를 디스크에 씁니다(폴더가 없으면 만듭니다). 저장 입구(직렬화기 · 형식의 `saveToFile`)가 모두 이 함수를 지납니다 — 있던 파일의 조건을 지키고(`restoreConditions`), 덮어쓰기 층을 얹어 읽은 리소스는 층 값이 원본에 박히므로 쓰지 않습니다.
         * @param pTagOverride 사본을 바꿔 끼운 태그로 읽었으면 그 축입니다(`ResourceContext::withTagOverride`) — 조건을 되돌릴 때 읽은 태그와 같아야 편집이 맞는 조건에 들어간다. nullptr 이면 엔진 태그입니다.
         * @return 쓰지 못하면 false 입니다(까닭은 로그).
         */
        [[nodiscard]] static bool saveText( string_view absPath, string_view text, const ResourceTagSet* pTagOverride = nullptr );

        /**
         * @brief 부모와의 차이 글 @p deltaText 를 다시 읽어 펼친 글 @p fullText 와 같은지 봅니다. @p reread 가 비었으면 보지 않습니다.
         * @details 칸을 형식 기본값으로 되돌린 편집은 차이로 적을 수 없다. 그때 부모 없는 한 벌로 쓰면 변형이 끊기므로 쓰지 않고 알린다(언리얼 Blueprint 의 CDO 차이 저장, Godot 씬 상속의 프로퍼티 복원과 같은 자리 — 기억에 의존).
         * @return 다르면 그 까닭입니다(로그는 부르는 쪽이 남깁니다).
         */
        [[nodiscard]] static expected<void> checkDeltaReadsBack( const string& deltaText, string_view fullText, string_view baseID, const ResourceRereadFunction& reread );
        /** @brief 차이를 만들어(`DataInheritance::makeDelta`) 다시 읽어 봅니다(`checkDeltaReadsBack`). 편집 창처럼 까닭을 화면에 적는 쪽이 부릅니다. */
        [[nodiscard]] static expected<void> makeCheckedDelta( string_view fullText, string_view parentFullText, const DataInheritanceSchema& schema, string_view baseID, const ResourceRereadFunction& reread, string& outDeltaText );
        /**
         * @brief `_base` 문서 저장의 한 벌입니다. 부모와의 차이를 만들고, 다시 읽어 대조하고, 씁니다. 실패는 오류 로그(`ResourceSave.DeltaNotSaved` · `ResourceSave.WriteFailed`)를 남기고 false 입니다.
         * @param reread 다시 읽어 펼친 글을 돌려주는 함수입니다(읽지 못하면 빈 글). 비면 대조하지 않습니다.
         */
        [[nodiscard]] static bool saveDelta( string_view absPath, string_view fullText, string_view parentFullText, const DataInheritanceSchema& schema, string_view baseID, const ResourceRereadFunction& reread = nullptr );
    };
} // namespace sw
