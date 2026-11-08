/**
 * @file ConditionExpr.h
 * @brief 조건 어휘입니다. 등록 줄 `SW_CONDITION` 과, 글을 읽을 때 후위 배열과 함수 포인터로 컴파일한 식 `ConditionExpr` 를 정의합니다.
 * @details 문법은 우선순위가 낮은 것부터 `a || b`, `a && b`, `!a`, 비교(`== != < <= > >=`), 더하기 · 빼기(`+ -`), 곱하기 · 나누기(`* /`), 괄호, 그리고 피연산자다.
 *          산술은 수치 칸(지역 프로필의 피해 · 배율 · 세기)이 조건과 같은 문법 · 문맥으로 값을 내려고 있다 — `20 * (1 - target.FireResist)`, `5 + timeInside * 2`.
 *          0 으로 나누면 값이 없음(NaN)이다. 피연산자는 다음 넷이다.
 *          - 수: `3`, `-2`, `0.5`
 *          - 호출: `weather(rain, storm)`, `tag(Quest.Done)` — 등록되지 않은 이름은 오류다
 *          - 인자 없는 등록 이름: `affinity >= 4`
 *          - 그 밖의 이름: 지정한 조건(`pBareCondition`, 기본 `flag`)에 그 이름을 인자로 넘긴다. 그래서 `hasKey && count >= 3` 이 플래그 식이다
 *          빈 식은 참이다. 값이 없음(NaN, 예: 없는 블랙보드 키)은 어떤 비교에도 거짓이고 참거짓으로 보면 거짓이다.
 *
 *          **읽을 때 컴파일한다.** 평가에 글이 없으므로 매 틱 불러도 파싱 비용이 없고, 틀린 식과 모르는 이름은 로드 오류가 된다. 언리얼 Gameplay Tag Query 가 쿼리를 바이트코드로 들고 평가하는 것과 같다.
 *          조건은 부수 효과가 없으므로 양쪽을 다 평가해도 된다(단락 평가를 하지 않아 식 모양이 곧 비용이다).
 *          호출마다 글 순서의 자리 번호가 있고, 둘째 호출부터는 그 번호를 섞은 씨앗으로 부른다 — `chance(0.5) && chance(0.5)` 는 독립 굴림 둘이다.
 *          평가 함수는 게임 모듈의 코드일 수 있으므로 핫 리로드 뒤에는 식을 다시 컴파일한다(`BehaviorTree` 의 함수 포인터와 같은 규칙).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Behavior/GameplayContext.h"
#include "Engine/Reflection/InstancedStruct.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 인자 없는 조건 · 동작의 칸입니다. */
    REFLECT()
    struct SW_API VocabularyNoParams
    {
        REFLECT_BODY();
    };

    /** @brief 이름 하나를 받는 조건의 칸입니다(`flag(hasKey)`). */
    REFLECT()
    struct SW_API ConditionNameArg
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
    };

    /** @brief 이름 목록을 받는 조건의 칸입니다(`weather(rain, storm)`). 목록에 든 값이면 참이다. */
    REFLECT()
    struct SW_API ConditionNameListArgs
    {
        REFLECT_BODY();

        PROPERTY()
        vector<hashed_string> _listName{};
    };

    /** @brief 수 하나를 받는 조건의 칸입니다(`chance(0.5)`). */
    REFLECT()
    struct SW_API ConditionNumberArg
    {
        REFLECT_BODY();

        PROPERTY( Required )
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 지운 조건 함수입니다. 수를 돌려주며 0 과 NaN 은 거짓입니다. */
    using ConditionFunction = float64 ( * )( const GameplayContext& context, const void* pArgs );

    /** @brief 조건 이름 하나의 등록 줄입니다. 글자는 등록한 이미지의 정적 저장소에 있습니다. */
    struct ConditionEntry
    {
        const utf8*               _pName;
        const InstancedStructOps* _pArgsOps;
        ConditionFunction         _function;
    };

    /** @brief `float64( const GameplayContext&, const TArgs& )`(참거짓 반환도 된다) 함수를 형식을 지운 함수로 옮깁니다. */
    template <typename TArgs, auto pFunction>
    struct ConditionThunk
    {
        static float64 call( const GameplayContext& context, const void* pArgs )
        {
            return static_cast<float64>( pFunction( context, *static_cast<const TArgs*>( pArgs ) ) );
        }
    };

    /**
     * @struct ConditionRegistry
     * @brief 이름 → 조건 표입니다. Engine 이미지의 함수 정적이라 모듈 경로 없이 도는 테스트 실행 파일에서도 등록이 보이고, 교체 이미지의 같은 이름은 대체합니다(`RegistrationReloadScope`).
     * @details 등록은 모듈을 올리는 스레드에서, 찾기는 데이터를 읽는 스레드에서 오므로 잠금으로 지킨다. 프레임마다 도는 경로가 아니다(읽을 때만 찾는다).
     */
    struct SW_API ConditionRegistry
    {
        static void registerCondition( const ConditionEntry* pEntry );
        static void unregisterCondition( const ConditionEntry* pEntry );
        /** @brief 이름(대소문자 구분)의 등록 줄입니다. 없으면 nullptr 입니다. */
        static const ConditionEntry* find( string_view name );
        /** @brief 등록된 이름을 등록 순서대로 뒤에 붙입니다(편집기 목록 · 오류 문장). */
        static void collectNames( vector<string>& outListName );
    };

    /** @brief `SW_CONDITION` 이 두는 정적 등록자입니다. */
    class SW_API ConditionRegistrar
    {
    public:
        explicit ConditionRegistrar( const ConditionEntry* pEntry );
        ~ConditionRegistrar();
        ConditionRegistrar( const ConditionRegistrar& )            = delete;
        ConditionRegistrar& operator=( const ConditionRegistrar& ) = delete;

    private:
        const ConditionEntry* _pEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief 후위 배열의 명령입니다. */
    enum class ConditionOpCode : uint8
    {
        PushNumber = 0,
        Call,
        Not,
        And,
        Or,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Add,
        Subtract,
        Multiply,
        Divide
    };

    /** @brief 명령 하나입니다. 호출이면 함수와 인자, 호출 자리 번호, 수이면 값을 씁니다. */
    struct ConditionInstruction
    {
        float64           _number{ 0.0 };
        ConditionFunction _function{ nullptr };
        const void*       _pArgs{ nullptr };
        uint32            _callIndex{ 0 }; ///< 식 안 호출의 글 순서(0 부터). 평가가 씨앗에 섞는다(`GameplayContext::mixCallSeed`)
        ConditionOpCode   _opCode{ ConditionOpCode::PushNumber };
    };

    /**
     * @class ConditionExpr
     * @brief 컴파일된 식 하나입니다. 복사해도 인자는 나눠 듭니다(인자는 읽기 전용이다). 여러 스레드가 같은 식을 평가해도 됩니다.
     */
    class SW_API ConditionExpr
    {
    public:
        static constexpr int32       kMaxDepth       = 32;     ///< 괄호 · `!` 중첩 상한(잘못된 식이 스택을 다 쓰지 않게)
        static constexpr int32       kMaxStack       = 64;     ///< 평가 스택 상한. 넘는 식은 읽기 오류다
        static constexpr const utf8* kBareFlag       = "flag"; ///< 그냥 이름을 넘기는 기본 조건
        static constexpr float64     kEqualTolerance = 1.0e-5;

        ConditionExpr();

        /**
         * @brief 글을 컴파일합니다. 틀린 글이나 모르는 이름이면 까닭을 오류 로그(`ConditionExpr.Invalid`)로 남기고 false 이며 @p outExpr 는 빈 식입니다.
         * @param sourceName 오류에 적는 이름입니다(파일과 항목).
         * @param pBareCondition 그냥 이름을 넘길 조건입니다. `_when` 은 문맥 태그 조건을 넘긴다. nullptr 이면 그냥 이름은 오류입니다.
         */
        [[nodiscard]] static bool compile( string_view text, string_view sourceName, ConditionExpr& outExpr, const utf8* pBareCondition = kBareFlag );
        /** @brief 로그를 남기지 않는 `compile` 입니다. 틀리면 그 까닭입니다. 편집기가 칸을 고칠 때마다 부릅니다. */
        [[nodiscard]] static expected<void> tryCompile( string_view text, ConditionExpr& outExpr, const utf8* pBareCondition = kBareFlag );

        /** @brief 평가합니다. 빈 식은 참입니다. */
        bool evaluate( const GameplayContext& context ) const;
        /** @brief 평가해 수를 돌려줍니다. 빈 식은 1 입니다. */
        float64 evaluateNumber( const GameplayContext& context ) const;
        /** @brief 지금 식의 값을 @p op 로 @p number 와 비교하는 식으로 늘입니다. 코드로 짓는 트리(`BehaviorTree::addBlackboardCondition`)가 씁니다. */
        void appendCompare( ConditionOpCode op, float64 number );

        bool          isEmpty() const { return _listInstruction.empty(); }
        const string& getText() const { return _text; }

        /**
         * @brief 식이 부르는 조건마다 @p visit( 함수, 인자 ) 를 식의 순서대로 부릅니다. 인자는 그 조건의 칸 구조체입니다.
         * @details 함수 포인터는 무엇을 묻는지 답하지 못한다 — 식을 읽는 쪽이 "어느 이름을 묻는가" 를 알아야 할 때 쓴다(리소스 조건 `_when` 이 쿠킹 축과 로드 축을 가른다).
         */
        template <typename TVisit>
        void forEachCall( TVisit&& visit ) const
        {
            for ( const ConditionInstruction& instruction : _listInstruction )
            {
                if ( instruction._opCode == ConditionOpCode::Call )
                    visit( instruction._function, instruction._pArgs );
            }
        }

    private:
        friend struct ConditionExprInternal;

        vector<ConditionInstruction>   _listInstruction;
        vector<shared_ptr<const void>> _listArgs; ///< 호출 명령의 인자를 쥔다
        string                         _text;     ///< 읽은 글(오류 문장 · 추적 · 같은 질문 비교)
    };
} // namespace sw

/**
 * @brief 조건을 그 함수의 .cpp 에서 등록합니다(`namespace sw` 안, 파일 범위). 이름이 곧 데이터가 적는 이름입니다.
 * @param name      식별자 이름(`weather`)
 * @param TArgs     칸 구조체(`ConditionNameListArgs`). 없으면 `VocabularyNoParams`
 * @param pFunction `float64( const GameplayContext&, const TArgs& )` 함수의 주소(참거짓 반환도 된다)
 */
#define SW_CONDITION( name, TArgs, pFunction )                                                                                                                 \
    static const ::sw::ConditionEntry     sw_condition_##name{ #name, &::sw::InstancedStructOps::of<TArgs>(), &::sw::ConditionThunk<TArgs, pFunction>::call }; \
    static const ::sw::ConditionRegistrar sw_conditionRegistrar_##name                                                                                         \
    {                                                                                                                                                          \
        &sw_condition_##name                                                                                                                                   \
    }
