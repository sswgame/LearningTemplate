/**
 * @file ResourceTags.h
 * @brief 리소스 조건(`_when`)이 묻는 문맥 태그입니다. 태그는 `축.값`(`platform.windows` · `quality.low` · `layer.Winter`)이고, 축마다 정해지는 때(쿠킹 · 로드)가 다릅니다.
 * @details 조건식은 행동 어휘와 같은 `ConditionExpr` 이고 그냥 이름은 태그 조건(`resourceTag`)으로 간다 — `<_when _if="quality.low || platform.linux" …/>`.
 *          축은 코드 표 하나다. 축마다 값을 넣는 코드(엔진 기동 · 사용자 설정 핸들러 · 현지화 · 데이터 레이어)가 따로 있어 데이터로 늘어나는 정책이 아니기 때문이다(값은 데이터다).
 *          - 쿠킹 축(`platform` · `target`): 쿠커가 펼쳐 쿠킹본에 남지 않는다. 런타임도 같은 값을 들고 있어(엔진 기동이 넣는다) 남은 식도 쿠커와 같은 답을 낸다.
 *          - 로드 축(`quality` · `locale` · `region` · `layer` · `feature` · `flag`): 쿠킹본에 `_when` 이 남고 읽을 때 정한다. 값이 바뀌면 그 태그를 읽은 리소스를 다시 읽는다(`ResourceComposition`).
 *          값은 엔진에 하나다 — 공유 캐시의 열쇠가 경로뿐이라 문맥이 둘이면 같은 경로가 두 결과를 가져야 한다(`docs/ResourceStructure.md` 의 한계). 주인은 코어 범위의 리소스 루트
 *          (`ContentRootSubsystem`)이고 엔진 코드는 `engine::getSubsystem<ResourceTags>()` 로, 서비스 표가 서기 전의 기동 코드는 `CoreParts::_pContentRoot` 로 닿는다.
 *
 *          언리얼은 플랫폼 ini 층과 Device Profile(쿠킹 · 실행 때 CVar 를 고른다), Godot 은 기능 태그(`feature tag`)로 프로젝트 설정 칸을 `칸.태그` 로 덮고,
 *          유니티는 플랫폼별 임포트 설정이 쿠킹 축만 갖는다(모두 기억에 의존). 여기는 Godot 처럼 태그 집합 하나에 묻되, 언리얼처럼 쿠킹 축과 실행 축을 나눠 쿠킹 축은 쿠킹본에서 지운다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Behavior/ConditionExpr.h"

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

    /** @brief 축의 값이 플레이어마다 달라도 되는가입니다(접속 합성 대조가 본다). */
    enum class ResourceTagScope : uint8
    {
        Shared,    ///< 모두 같아야 한다 — 다르면 판정이 어긋난다
        PerClient, ///< 사용자 설정이다(품질 · 언어) — 달라도 대조하지 않는다. 판정 값은 이 축에 기대지 않는다
    };

    /** @brief 축 하나의 정의입니다(`ResourceTags::findAxis`). */
    struct ResourceTagAxis
    {
        const utf8*        _pName;
        ResourceTagTiming  _timing;
        ResourceTagArity   _arity;
        const utf8* const* _ppValue;    ///< 닫힌 값 목록(nullptr 이면 값은 데이터가 정한다 — 품질 이름은 설정 스키마가 낸다)
        uint32             _valueCount; ///< `_ppValue` 의 수
        ResourceTagScope   _scope;      ///< 플레이어마다 달라도 되는가
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
        /**
         * @brief @p overrideTags 의 태그마다 그 축의 태그를 끄고 그 태그를 켭니다(값 하나 축을 바꿔 끼운다).
         * @details 미리보기 문맥(에디터 UI 미리보기의 기기 견본)이 엔진 값에서 몇 축만 바꿔 합칠 때 쓴다(`ResourceContext::withTagOverride`).
         */
        void overrideAxes( const ResourceTagSet& overrideTags );
        /** @brief 켜진 태그가 모두 같으면 true 입니다. */
        bool operator==( const ResourceTagSet& other ) const { return _listTag == other._listTag; }
        bool operator!=( const ResourceTagSet& other ) const { return ( *this == other ) == false; }
        /** @brief 켜진 태그입니다(이름 순). */
        const vector<string>& getTags() const { return _listTag; }

    private:
        vector<string> _listTag;
    };

    /** @brief 조건 문맥(`GameplayContext`)에서 태그 조건이 읽는 칸입니다. */
    struct ResourceTagSlot
    {
        using Type                      = const ResourceTagSet;
        static constexpr uint32 kSlotID = FourCCUtil::make( "RTAG" );
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

    class ResourceComposition;

    /**
     * @class ResourceTags
     * @brief 엔진의 태그 값과 축 표입니다. 넣는 쪽은 게임 스레드, 읽는 쪽은 로드 워커라 잠금으로 지킵니다. 값이 바뀌면 그 태그에 기댄 리소스를 다시 읽기 기록(`ResourceComposition`)에 알립니다.
     * @details 예전에는 함수 정적 상태(감춘 싱글턴)였다 — 엔진을 다시 세워도 남고 테스트가 서로 섞였다. 축 표 · 조건 컴파일처럼 상태가 없는 것만 정적이다.
     */
    class SW_API ResourceTags
    {
    public:
        static constexpr const utf8* kConditionName = "resourceTag"; ///< 그냥 이름을 받는 조건(`ConditionExpr` 의 `pBareCondition`)
        static constexpr utf8        kSeparator     = '.';           ///< 축과 값을 가른다

        /** @brief @p composition 은 같은 주인(리소스 루트)이 든 다시 읽기 기록이다(이것보다 먼저 서고 나중에 사라진다). */
        explicit ResourceTags( ResourceComposition& composition );

        ResourceTags( const ResourceTags& )            = delete;
        ResourceTags& operator=( const ResourceTags& ) = delete;

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
        [[nodiscard]] bool setAxisValue( string_view axisName, string_view value );
        /** @brief 여러 값 축 @p axisName 의 값을 @p listValue 로 바꿉니다. 틀리면 `setAxisValue` 처럼 false 입니다. */
        [[nodiscard]] bool setAxisValues( string_view axisName, const vector<string>& listValue );
        /** @brief 여러 값 축 @p axisName 의 값 @p value 를 켜거나 끕니다. 틀리면 `setAxisValue` 처럼 false 입니다. */
        [[nodiscard]] bool setTagActive( string_view axisName, string_view value, bool bActive );
        /**
         * @brief 값 하나 축들의 값을 한 번에 바꿉니다 — @p tags 의 태그(`축.값`)마다 그 축을 그 값으로 둡니다(알림은 한 번).
         * @return 모양이 틀렸거나 · 모르는 축 · 여러 값 축 · 닫힌 목록 밖의 값이면 오류를 남기고 false 입니다(값을 바꾸지 않는다).
         */
        [[nodiscard]] bool setAxisTags( const ResourceTagSet& tags );
        /** @brief 지금 켜진 태그의 사본입니다. */
        [[nodiscard]] ResourceTagSet capture() const;
        /** @brief 지금 켜진 태그에 @p overrideTags 의 축을 바꿔 끼운 사본입니다(`ResourceTagSet::overrideAxes`). 엔진 값은 그대로입니다. */
        [[nodiscard]] ResourceTagSet captureWith( const ResourceTagSet& overrideTags ) const;
        /** @brief 태그 집합을 통째로 바꾸고 앞의 것을 돌려줍니다. 테스트가 문맥을 세우고 되돌릴 때 씁니다 — 바뀐 태그를 쓴 리소스는 다시 읽을 목록에 오른다. */
        ResourceTagSet exchange( const ResourceTagSet& tags );

        /**
         * @brief `_if` 글을 컴파일합니다. 그냥 이름은 태그이고, 태그 조건 말고 다른 조건(`flag` · `chance`)을 부르거나 모르는 축 · 닫힌 목록 밖의 값을 쓰면 오류입니다.
         * @details 게임 상태 조건을 막는 까닭: 리소스 문맥에는 게임 세계 칸이 없어 "모름" 으로 늘 거짓이 된다. 게임 플래그는 `flag` 축 태그로 넣는다(게임 코드가 `setTagActive`).
         * @return 틀리면 까닭을 오류 로그(`ResourceCondition.Invalid`)로 남기고 false 입니다.
         */
        [[nodiscard]] static bool compileTest( string_view text, string_view sourceName, ResourceTagTest& outTest );

    private:
        /** @brief 태그 집합을 바꾸고 바뀐 태그를 다시 읽기 기록에 알립니다. 잠금 밖에서 알린다(기록은 자기 잠금이 있다). */
        void replace( const ResourceTagSet& next, ResourceTagSet* pOutPrevious );

    private:
        ResourceComposition& _composition;
        mutable mutex        _mutex;
        ResourceTagSet _tags SW_GUARDED_BY( _mutex );
    };

    SW_SERVICE_SLOT( ResourceTags, Required );
} // namespace sw
