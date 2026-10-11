/**
 * @file FitOperator.h
 * @brief 피팅 연산의 계약(`IFitOperator`)과 이름 붙은 레지스트리(`FitOperatorRegistry`)입니다. 상호작용 테이블이 연산을 이름으로 고릅니다.
 * @details 연산은 단계 하나에 속한다. 변형(`Deform`)은 조임과 밀어내기이며 야코비 반복 안에서 정점 변위를 더한다. 덮임(`Coverage`)은 잘라 내기이며 덮인
 *          삼각형을 표시한다. 검증(`Validate`)은 보고한다. 새 효과는 연산 하나를 등록하면 테이블에서 쓸 수 있다. 기본 연산 넷(`Shrink`, `Push`, `Cut`, `Report`)은
 *          `registerDefaultOperators` 가 등록한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct FitInteractionDef;
    struct FitSolveState;

    /** @brief 연산이 도는 단계입니다(이 순서로 돕니다). */
    enum class FitPhase : uint8
    {
        Deform = 0, ///< 정점을 옮긴다(조임, 밀어내기). 몇 번의 야코비 반복 안에서 실행한다.
        Coverage,   ///< 바깥 겹에 덮인 안쪽 삼각형을 표시한다(잘라 내기). 변형 뒤의 위치로 판정한다.
        Validate,   ///< 고치지 않고 보고한다(단단한 부품끼리의 관통).
    };

    /** @brief 연산 한 번이 받는 쌍입니다. 안쪽 부품과 바깥 부품(입력 순서 번호), 그리고 그 항목을 담습니다. */
    struct FitPairContext
    {
        const FitInteractionDef* _pInteraction{ nullptr };
        uint32                   _innerPart{ 0 };
        uint32                   _outerPart{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 연산 하나입니다. 상태가 없어야 합니다(같은 인스턴스를 여러 짝에 부릅니다). */
    class SW_API IFitOperator
    {
    public:
        IFitOperator()                                 = default;
        virtual ~IFitOperator()                        = default;
        IFitOperator( const IFitOperator& )            = delete;
        IFitOperator& operator=( const IFitOperator& ) = delete;

        /** @brief 테이블이 연산을 고를 때 쓰는 이름입니다. */
        virtual const hashed_string& getName() const = 0;
        /** @brief 도는 단계입니다. */
        virtual FitPhase getPhase() const = 0;
        /** @brief 이 인자 이름을 아는지 반환합니다. 테이블을 로드할 때 호출합니다(모르는 인자는 오류입니다). */
        virtual bool acceptsArgument( const hashed_string& name ) const = 0;
        /** @brief 짝 하나에 연산을 겁니다. */
        virtual void execute( FitSolveState& state, const FitPairContext& pair ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 → 연산 레지스트리입니다. */
    class SW_API FitOperatorRegistry
    {
    public:
        FitOperatorRegistry();
        ~FitOperatorRegistry();
        FitOperatorRegistry( const FitOperatorRegistry& )            = delete;
        FitOperatorRegistry& operator=( const FitOperatorRegistry& ) = delete;

        /** @brief 연산을 올립니다. 같은 이름이 이미 있으면 올리지 않고 false 입니다. */
        bool registerOperator( unique_ptr<IFitOperator> fitOperator );
        /** @brief 이름의 연산입니다. 없으면 nullptr 입니다. */
        const IFitOperator* findOperator( const hashed_string& name ) const;
        /** @brief 기본 연산 넷(`Shrink`, `Push`, `Cut`, `Report`)을 등록합니다. */
        void registerDefaultOperators();
        /** @brief 등록한 연산 수를 반환합니다. */
        uint32 getOperatorCount() const { return _registry.getCount(); }

    private:
        NameRegistry<unique_ptr<IFitOperator>> _registry;
    };
} // namespace sw
