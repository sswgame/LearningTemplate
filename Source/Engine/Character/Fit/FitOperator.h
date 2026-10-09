/**
 * @file FitOperator.h
 * @brief 피팅 연산의 계약(`IFitOperator`)과 이름 붙은 등록부(`FitOperatorRegistry`)입니다. 상호작용 표가 연산을 이름으로 고릅니다.
 * @details 연산은 단계 하나에 속합니다 — 변형(`Deform`: 조임 · 밀어내기, 야코비 반복 안에서 정점 변위를 더함), 덮임(`Coverage`: 잘라 내기, 덮인
 *          삼각형을 표시), 검증(`Validate`: 보고). 새 효과는 연산 하나를 등록하면 표에서 씁니다. 기본 넷은 `registerDefaultOperators` 가 올립니다:
 *          `Shrink` · `Push` · `Cut` · `Report`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/RegistrationList.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct FitInteractionDef;
    struct FitSolveState;

    /** @brief 연산이 도는 단계입니다(이 순서로 돕니다). */
    enum class FitPhase : uint8
    {
        Deform = 0, ///< 정점을 옮긴다(조임 · 밀어내기) — 몇 번의 야코비 반복 안에서
        Coverage,   ///< 바깥 겹에 덮인 안쪽 삼각형을 표시한다(잘라 내기) — 변형 뒤의 자리로 판정
        Validate,   ///< 고치지 않고 보고한다(단단함끼리의 관통)
    };

    /** @brief 연산 한 번이 받는 짝 — 안쪽 부품 · 바깥 부품(입력 순서 번호)과 그 줄입니다. */
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

        /** @brief 표가 고르는 이름입니다. */
        virtual const hashed_string& getName() const = 0;
        /** @brief 도는 단계입니다. */
        virtual FitPhase getPhase() const = 0;
        /** @brief 이 인자 이름을 아는가입니다. 표 로드가 묻습니다(모르는 인자는 오류). */
        virtual bool acceptsArgument( const hashed_string& name ) const = 0;
        /** @brief 짝 하나에 연산을 겁니다. */
        virtual void execute( FitSolveState& state, const FitPairContext& pair ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 → 연산 등록부입니다. */
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
        /** @brief 기본 연산 넷(`Shrink` · `Push` · `Cut` · `Report`)을 올립니다. */
        void registerDefaultOperators();
        /** @brief 올린 연산 수입니다. */
        uint32 getOperatorCount() const { return _registry.getCount(); }

    private:
        NameRegistry<unique_ptr<IFitOperator>> _registry;
    };
} // namespace sw
