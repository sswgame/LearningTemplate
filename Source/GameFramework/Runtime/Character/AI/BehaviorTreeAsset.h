/**
 * @file BehaviorTreeAsset.h
 * @brief 행동 트리 데이터 파일(`*.bt.xml`, 데이터 종류 `behaviortree`)을 읽어 `BehaviorTree` 를 짓습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    struct BehaviorTreeNodeData;

    /**
     * @struct BehaviorTreeNames
     * @brief 노드 종류 · 블랙보드 비교 · 중단 규칙의 이름과 종류 분류입니다. 이름은 열거형의 리플렉션(`ENUM()`)이 정본이다 — 따로 든 이름표가 없다.
     */
    struct SW_GF_API BehaviorTreeNames
    {
        static const utf8* toString( BehaviorNodeKind type );
        static const utf8* toString( BehaviorAbortMode mode );
        /** @brief 이름(대소문자 무시)으로 열거자를 찾습니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        [[nodiscard]] static bool parse( string_view text, BehaviorNodeKind& outValue );
        [[nodiscard]] static bool parse( string_view text, BehaviorAbortMode& outValue );
        /** @brief 모든 이름을 ", " 로 이은 글입니다(모르는 이름 오류에 보인다). */
        static string joinNodeTypeNames();
        /** @brief 자식을 가질 수 없는 종류(작업 · 조건 · 대기 · 이동 · 하위 트리)입니다. */
        static bool isLeaf( BehaviorNodeKind type );
        /** @brief 자식이 정확히 하나인 종류(반전 · 성공 강제 · 반복 · 쿨다운 · 시간 제한 · 블랙보드 조건)입니다. */
        static bool isDecorator( BehaviorNodeKind type );
        /** @brief 중첩 깊이 상한입니다. 손으로 쓴 트리가 이보다 깊을 일이 없다 - 깨진 파일이 스택을 먹지 않게 한다(편집기도 이 깊이까지만 더한다). */
        static constexpr uint32 kMaxDepth = 32;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct BehaviorTreeXML
     * @brief 노드 원소 하나의 칸 읽기 · 쓰기입니다. 런타임 로더와 에디터 모델이 함께 쓴다(같은 형식을 두 곳이 손으로 읽지 않는다).
     * @details 종류마다 받는 칸은 스키마 구조체(`BehaviorTreeNodeParams.h`)의 `PROPERTY` 가 정한다 — 모르는 속성 · 빠진 필수 · 범위 밖 값은 `XMLFields::readFields` 가 이름과 함께 알린다.
     */
    struct SW_GF_API BehaviorTreeXML
    {
        /** @brief 종류 @p type 의 스키마 값을 기본값으로 담습니다(`_type` 도 채운다). 칸이 있던 작업 동작은 비웁니다. */
        static void resetNodeFields( BehaviorNodeKind type, BehaviorTreeNodeData& inoutData );
        /** @brief 원소 @p node(종류 @p type)의 속성을 @p inoutData 에 읽습니다. 틀린 속성이 하나라도 있으면 false 입니다(오류는 로그에 남는다). 등록되지 않은 동작의 칸은 글로 듭니다(`_listRawParam`). */
        [[nodiscard]] static bool readNodeFields( const XMLNode& node, BehaviorNodeKind type, string_view sourceName, BehaviorTreeNodeData& inoutData );
        /** @brief @p data 의 칸을 @p node 에 속성으로 씁니다(기본값은 생략 — 손으로 쓴 파일과 같은 모양). */
        static void writeNodeFields( const XMLNode& node, const BehaviorTreeNodeData& data );
        /** @brief 작업 노드의 동작을 @p task 로 바꿉니다. 칸은 그 동작의 기본값이 됩니다. 등록되지 않은 이름이면 칸이 비어 있습니다. */
        static void selectTask( BehaviorTreeNodeData& inoutData, string_view task );
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
     *             <BlackboardCondition test="hasBlackboard(Threat)" abort="LowerPriority">
     *                 <Action task="rts.defend" name="Defend"/>
     *             </BlackboardCondition>
     *             <Sequence name="Economy">
     *                 <ForceSuccess><Action task="rts.gatherIdle" name="GatherIdle"/></ForceSuccess>
     *             </Sequence>
     *         </Selector>
     *     </BehaviorTree>
     * @endcode
     * @details **노드 원소와 속성**(원소 이름은 `BehaviorNodeKind` 의 열거자 이름이고 대소문자를 가리지 않는다. 형제 순서가 곧 우선순위다).
     *          종류마다 받는 속성 · 필수 · 범위는 스키마 구조체(`BehaviorTreeNodeParams.h`)가 정본이고 `BehaviorTreeXML` 이 읽는다 — 아래는 그 요약이다(`compare` · `abort` 값은 열거자 이름 그대로):
     *          - `Selector` (`name`, `reactive`) · `Sequence` (`name`) · `Inverter` · `ForceSuccess` — 자식은 컴포짓이 하나 이상, 데코레이터가 정확히 하나다.
     *          - `Repeat` (`count` 필수, 0 이면 실패할 때까지) · `Cooldown` · `TimeLimit` (`seconds` 필수).
     *          - `BlackboardCondition` (`test` 필수 — 공통 조건식, `abort` 기본 `None` | `Self` | `LowerPriority` | `Both`) — 데코레이터다.
     *          - `Action` (`task` 필수 — `ActionRegistry` 에 등록된 동작 이름, 나머지 속성은 그 동작의 칸) · `Condition` (`test` 필수) — 잎이라 자식이 없다.
     *          - `Wait` (`seconds` 필수) · `MoveTo` (`key` 필수, `repathDistance` 기본 0.5) — 잎이다.
     *          - `SubTree` (`tree` 또는 `slot`) — `tree` 는 읽을 때 그 자리에 펼친다(순환은 오류). `slot` 은 쓰는 쪽이 `BehaviorTreeRunner::setSlotTree` 로 채운다.
     *          어느 노드든 `id` · `name` 을 적을 수 있다. 루트의 `_base="<리소스 id>"` 는 부모 트리에서 `id` 가 같은 노드만 고친다(`_op` · `_before` · `_after` — `docs/agents/DataInheritance.md`).
     *          모르는 원소 · 속성 · 중단 이름, 틀린 식, 등록되지 않은 `task` 는 오류이고 트리를 읽지 않는다(기본 노드로 바꾸지 않는다). 읽은 트리는 `BehaviorTree::isValid` 를 지나야 한다.
     */
    class SW_GF_API BehaviorTreeAsset : public XMLCatalog<BehaviorTreeAsset>
    {
        friend class XMLCatalog<BehaviorTreeAsset>;
        friend struct AssetTypeLoader;

    public:
        /**
         * @brief 데이터 종류 이름입니다(스키마 테스트 `ResourceDataSchemaTest` 의 표 줄 이름).
         * @details 경로 규칙(`AssetPathRule`)을 두지 않는다 - 경로 규칙은 카탈로그 편집기(표)의 파일 목록이기도 한데 이 종류에는 전용 편집기(`BehaviorTreePanel`)가 있어 같은 파일을 두 편집기가 쥐게 된다.
         */
        static constexpr const utf8* kDataKind = "behaviortree";
        /** @brief 파일 접미입니다. */
        static constexpr string_view kExtension = ".bt.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 경로 규칙이 아니라 접미사라 카탈로그 편집기에는 나오지 않는다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `BehaviorTreeAsset.cpp` 의 공유 캐시 줄). 타입 있는 참조 `AssetRef<BehaviorTreeAsset>` 가 접미사로 가른다. */
        static constexpr AssetTypeDef kAssetType{ kDataKind, kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        BehaviorTreeAsset();

        /** @brief 글을 읽습니다. 부모와 하위 파일은 리소스에서 찾습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        /** @brief 글을 읽습니다. 부모와 하위 파일을 @p source 에서 찾습니다(테스트 · 도구). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, IDataInheritanceSource& source );

        /** @brief 읽은 트리입니다. 읽기에 실패했거나 읽기 전이면 빈 트리(`isValid` 가 false)입니다. 이 객체보다 오래 쓰려면 복사한다. */
        const BehaviorTree& getTree() const { return _tree; }

    private:
        static constexpr const utf8* kXMLRootName = "BehaviorTree"; ///< 루트 원소(`XMLCatalog`)

        /**
         * @brief 리소스 경로의 트리를 읽습니다. `_base` 를 펼치고 하위 트리를 읽을 때 부모와 하위 파일을 리소스에서 찾습니다. 없으면 false 입니다.
         * @details 베이스(`XMLCatalog`)의 보호된 읽기를 가리는 자기 읽기라 공개하면 사본을 읽는 길이 다시 열린다 — 종류의 로더(공유 캐시)만 부른다.
         */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        uint32             loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 부모를 펼친 글을 읽습니다. 하위 파일은 @p source 에서 찾습니다. */
        [[nodiscard]] bool readFlatText( string_view flatText, string_view sourceName, IDataInheritanceSource& source );

    private:
        BehaviorTree            _tree;
        IDataInheritanceSource* _pSource; ///< 읽는 동안만 쓴다 — 하위 트리 파일을 찾는 곳
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<BehaviorTreeAsset>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, BehaviorTreeAsset );
} // namespace sw
