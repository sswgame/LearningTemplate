/**
 * @file BehaviorTreeAsset.h
 * @brief 행동 트리 데이터 파일(`*.bt.xml`, 데이터 종류 `behaviortree`)을 읽어 `BehaviorTree` 를 짓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct BehaviorTreeNames
     * @brief 노드 종류 · 블랙보드 비교 · 중단 규칙의 데이터 파일 이름표입니다. 로더(`BehaviorTreeAsset`)와 행동 트리 편집기가 같은 표를 쓴다 - 열거자를 더하면 이 표 한 곳만 맞춘다.
     */
    struct SW_GF_API BehaviorTreeNames
    {
        static const utf8* toString( BehaviorNodeType type );
        static const utf8* toString( BlackboardCompare compare );
        static const utf8* toString( BehaviorAbortMode mode );
        /** @brief 이름(대소문자 무시)으로 열거자를 찾습니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        [[nodiscard]] static bool parse( string_view text, BehaviorNodeType& outValue );
        [[nodiscard]] static bool parse( string_view text, BlackboardCompare& outValue );
        [[nodiscard]] static bool parse( string_view text, BehaviorAbortMode& outValue );
        /** @brief 모든 이름을 ", " 로 이은 글입니다(모르는 이름 오류에 보인다). */
        static string joinNodeTypeNames();
        /** @brief 자식을 가질 수 없는 종류(작업 · 조건 · 대기 · 이동)입니다. */
        static bool isLeaf( BehaviorNodeType type );
        /** @brief 자식이 정확히 하나인 종류(반전 · 성공 강제 · 반복 · 쿨다운 · 시간 제한 · 블랙보드 조건)입니다. */
        static bool isDecorator( BehaviorNodeType type );
    };
} // namespace sw

namespace sw
{
    /**
     * @class BehaviorTreeAsset
     * @brief 행동 트리 하나를 담은 데이터 파일입니다(언리얼 Behavior Tree 에셋에 해당). 원소 이름이 노드 종류이고 중첩이 트리의 부모 자식입니다.
     * @code
     *     <BehaviorTree>
     *         <Selector name="Root" reactive="true">
     *             <BlackboardCondition key="Threat" compare="IsSet" abort="LowerPriority">
     *                 <Action task="rts.defend" name="Defend"/>
     *             </BlackboardCondition>
     *             <Sequence name="Economy">
     *                 <ForceSuccess><Action task="rts.gatherIdle" name="GatherIdle"/></ForceSuccess>
     *             </Sequence>
     *         </Selector>
     *     </BehaviorTree>
     * @endcode
     * @details **노드 원소와 속성**(이름은 `BehaviorNodeType` 의 열거자 이름이고 대소문자를 가리지 않는다. 형제 순서가 곧 우선순위다):
     *          - `Selector` (`name`, `reactive`) · `Sequence` (`name`) · `Inverter` · `ForceSuccess` — 자식은 컴포짓이 하나 이상, 데코레이터가 정확히 하나다.
     *          - `Repeat` (`count` 필수, 0 이면 실패할 때까지) · `Cooldown` · `TimeLimit` (`seconds` 필수).
     *          - `BlackboardCondition` (`key` 필수, `compare` 기본 `IsSet`, `value` 기본 0, `abort` 기본 `None` | `Self` | `LowerPriority` | `Both`) — 데코레이터다.
     *          - `Action` · `Condition` (`task` 필수 — `BehaviorTaskRegistry` 에 등록된 함수 이름, `name` 선택) — 잎이라 자식이 없다.
     *          - `Wait` (`seconds` 필수) · `MoveTo` (`key` 필수, `repathDistance` 기본 0.5) — 잎이다.
     *          모르는 원소 · 속성 · 비교 · 중단 이름과 등록되지 않은 `task` 는 오류이고 트리를 읽지 않는다(기본 노드로 바꾸지 않는다). 읽은 트리는 `BehaviorTree::isValid` 를 지나야 한다.
     */
    class SW_GF_API BehaviorTreeAsset : public XMLCatalog<BehaviorTreeAsset>
    {
        friend class XMLCatalog<BehaviorTreeAsset>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄이 쓴다). */
        static constexpr const utf8* kDataKind = "behaviortree";
        /** @brief 파일 접미입니다. */
        static constexpr string_view kExtension = ".bt.xml";

        BehaviorTreeAsset();

        /** @brief 읽은 트리입니다. 읽기에 실패했거나 읽기 전이면 빈 트리(`isValid` 가 false)입니다. 이 객체보다 오래 쓰려면 복사한다. */
        const BehaviorTree& getTree() const { return _tree; }

    private:
        static constexpr const utf8* kXMLRootName = "BehaviorTree"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        BehaviorTree _tree;
    };
} // namespace sw
