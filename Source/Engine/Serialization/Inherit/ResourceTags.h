/**
 * @file ResourceTags.h
 * @brief 리소스 조건(`_when`)이 묻는 문맥 태그입니다. 태그는 `축.값`(`platform.windows` · `quality.low` · `layer.Winter`)이고, 축마다 정해지는 때(쿠킹 · 로드)가 다릅니다.
 * @details 조건식은 행동 어휘와 같은 `ConditionExpr` 이고 그냥 이름은 태그 조건(`resourceTag`)으로 간다 — `<_when _if="quality.low || platform.linux" …/>`.
 *          축은 코드 표 하나다. 축마다 값을 넣는 코드(엔진 기동 · 사용자 설정 핸들러 · 현지화 · 데이터 레이어)가 따로 있어 데이터로 늘어나는 정책이 아니기 때문이다(값은 데이터다).
 *          - 쿠킹 축(`platform` · `target`): 쿠커가 펼쳐 쿠킹본에 남지 않는다. 런타임도 같은 값을 들고 있어(엔진 기동이 넣는다) 남은 식도 쿠커와 같은 답을 낸다.
 *          - 로드 축(`quality` · `locale` · `region` · `layer` · `feature` · `flag`): 쿠킹본에 `_when` 이 남고 읽을 때 정한다. 값이 바뀌면 그 태그를 읽은 리소스를 다시 읽는다(`ResourceComposition`).
 *          값은 프로세스에 하나다 — 공유 캐시의 열쇠가 경로뿐이라 문맥이 둘이면 같은 경로가 두 결과를 가져야 한다(`docs/ResourceStructure.md` 의 한계).
 *
 *          언리얼은 플랫폼 ini 층과 Device Profile(쿠킹 · 실행 때 CVar 를 고른다), Godot 은 기능 태그(`feature tag`)로 프로젝트 설정 칸을 `칸.태그` 로 덮고,
 *          유니티는 플랫폼별 임포트 설정이 쿠킹 축만 갖는다(모두 기억에 의존). 여기는 Godot 처럼 태그 집합 하나에 묻되, 언리얼처럼 쿠킹 축과 실행 축을 나눠 쿠킹 축은 쿠킹본에서 지운다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Gameplay/ConditionExpr.h"

namespace sw
{
    /** @brief 축의 값이 정해지는 때입니다. */
    enum class ResourceTagTiming : uint8
    {
        Cook, ///< 쿠킹 때 정한다 — 쿠커가 펼치고, 런타임도 같은 값을 든다
        Load, ///< 읽을 때 정한다 — 쿠킹본에 남고, 바뀌면 다시 읽는다
    };

    /** @brief 축 하나가 값을 몇 개 드는가입니다. */
    enum class ResourceTagArity : uint8
    {
        Single, ///< 값 하나(`quality.low` 와 `quality.high` 는 함께 켜지지 않는다)
        Multi,  ///< 값 여럿(켜진 데이터 레이어 · 기능 태그)
    };

    /** @brief 축 하나의 정의입니다(`ResourceTags::findAxis`). */
    struct ResourceTagAxis
    {
        const utf8*        _pName;
        ResourceTagTiming  _timing;
        ResourceTagArity   _arity;
        const utf8* const* _ppValue;    ///< 닫힌 값 목록(nullptr 이면 값은 데이터가 정한다 — 품질 이름은 설정 스키마가 낸다)
        uint32             _valueCount; ///< `_ppValue` 의 수
    };

    /**
     * @class ResourceTagSet
     * @brief 켜진 태그(`축.값`)의 정렬된 집합입니다. 값으로 복사해 넘깁니다(태그는 한 자릿수다).
     */
    class SW_API ResourceTagSet
    {
    public:
        ResourceTagSet();

        /** @brief @p tag 가 켜져 있으면 true 입니다. */
        [[nodiscard]] bool contains( string_view tag ) const;
        /** @brief @p tag 를 켭니다. 이미 있으면 그대로입니다. */
        void add( string_view tag );
        /** @brief @p tag 를 끕니다. */
        void remove( string_view tag );
        /** @brief 축 @p axisName 의 태그를 모두 끕니다. */
        void removeAxis( string_view axisName );
        /** @brief 켜진 태그입니다(이름 순). */
        const vector<string>& getTags() const { return _listTag; }

    private:
        vector<string> _listTag;
    };

    /** @brief 조건 문맥(`GameplayContext`)에서 태그 조건이 읽는 칸입니다. */
    struct ResourceTagSlot
    {
        using Type                      = const ResourceTagSet;
        static constexpr uint32 kSlotID = FourCcUtil::make( "RTAG" );
    };

    /** @brief 컴파일한 리소스 조건 하나입니다(`ResourceTags::compileTest`). */
    struct SW_API ResourceTagTest
    {
        ConditionExpr     _expr{};                            ///< 컴파일한 식
        vector<string>    _listLoadTag{};                     ///< 식이 묻는 로드 축 태그(다시 읽기 · 접속 대조가 쓴다)
        ResourceTagTiming _timing{ ResourceTagTiming::Cook }; ///< 묻는 태그가 모두 쿠킹 축이면 `Cook`

        /** @brief @p tags 로 평가합니다. */
        [[nodiscard]] bool evaluate( const ResourceTagSet& tags ) const;
    };

    /**
     * @struct ResourceTags
     * @brief 프로세스의 태그 값과 축 표입니다. 넣는 쪽은 게임 스레드, 읽는 쪽은 로드 워커라 잠금으로 지킵니다.
     */
    struct SW_API ResourceTags
    {
        static constexpr const utf8* kConditionName = "resourceTag"; ///< 그냥 이름을 받는 조건(`ConditionExpr` 의 `pBareCondition`)
        static constexpr utf8        kSeparator     = '.';           ///< 축과 값을 가른다

        /** @brief 이름이 @p axisName 인 축입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static const ResourceTagAxis* findAxis( string_view axisName );
        /** @brief 태그 @p tag(`축.값`)의 축입니다. 모양이 틀렸거나 모르는 축이면 nullptr 입니다. */
        [[nodiscard]] static const ResourceTagAxis* findAxisOfTag( string_view tag );
        /** @brief 축 이름을 표 순서대로 뒤에 붙입니다(오류 문장 · 편집기 목록). */
        static void collectAxisNames( vector<string>& outListName );

        /**
         * @brief 값 하나 축 @p axisName 의 값을 @p value 로 바꿉니다. 빈 값은 끕니다.
         * @return 모르는 축 · 여러 값 축 · 닫힌 목록에 없는 값이면 오류를 남기고 false 입니다(값을 바꾸지 않는다).
         */
        [[nodiscard]] static bool setAxisValue( string_view axisName, string_view value );
        /** @brief 여러 값 축 @p axisName 의 값을 @p listValue 로 바꿉니다. 틀리면 `setAxisValue` 처럼 false 입니다. */
        [[nodiscard]] static bool setAxisValues( string_view axisName, const vector<string>& listValue );
        /** @brief 여러 값 축 @p axisName 의 값 @p value 를 켜거나 끕니다. 틀리면 `setAxisValue` 처럼 false 입니다. */
        [[nodiscard]] static bool setTagActive( string_view axisName, string_view value, bool bActive );
        /** @brief 지금 켜진 태그의 사본입니다. */
        [[nodiscard]] static ResourceTagSet capture();
        /** @brief 태그 집합을 통째로 바꾸고 앞의 것을 돌려줍니다. 테스트가 문맥을 세우고 되돌릴 때 씁니다 — 바뀐 태그를 쓴 리소스는 다시 읽을 목록에 오른다. */
        static ResourceTagSet exchange( const ResourceTagSet& tags );

        /**
         * @brief `_if` 글을 컴파일합니다. 그냥 이름은 태그이고, 태그 조건 말고 다른 조건(`flag` · `chance`)을 부르거나 모르는 축 · 닫힌 목록 밖의 값을 쓰면 오류입니다.
         * @details 게임 상태 조건을 막는 까닭: 리소스 문맥에는 게임 세계 칸이 없어 "모름" 으로 늘 거짓이 된다. 게임 플래그는 `flag` 축 태그로 넣는다(게임 코드가 `setTagActive`).
         * @return 틀리면 까닭을 오류 로그(`ResourceCondition.Invalid`)로 남기고 false 입니다.
         */
        [[nodiscard]] static bool compileTest( string_view text, string_view sourceName, ResourceTagTest& outTest );
    };
} // namespace sw
