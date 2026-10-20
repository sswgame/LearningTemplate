/**
 * @file AbilityCatalog.h
 * @brief id 로 찾는 이펙트, 어빌리티, 어빌리티 세트의 정의와 이름으로 클래스를 만드는 레지스트리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AttributeSet.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayAbility.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayEffect.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 어빌리티 클래스 하나를 만드는 함수입니다(`AbilityCatalog::registerAbilityClass`). */
    using AbilityFactoryFunc = unique_ptr<GameplayAbility> ( * )();
    /** @brief 어트리뷰트 세트 클래스 하나를 만드는 함수입니다(`AbilityCatalog::registerAttributeSetClass`). */
    using AttributeSetFactoryFunc = unique_ptr<AttributeSet> ( * )();
    /** @brief XML `<Execution class="...">` 노드로 실행 계산을 만드는 함수입니다(`AbilityCatalog::registerExecutionClass`). */
    using ExecutionFactoryFunc = shared_ptr<const IGameplayEffectExecution> ( * )( const XMLNode& node );

    // ------------------------------------------------------------------------------
    // 1) 정의. 카탈로그가 보관하는 데이터
    // ------------------------------------------------------------------------------
    /** @brief 어빌리티 하나의 정의입니다. 어느 클래스로 만들고 어떤 설정을 줄지 담습니다. */
    struct GameplayAbilityDef
    {
        hashed_string         _id{};
        hashed_string         _className{}; ///< `registerAbilityClass` 로 등록한 이름
        GameplayAbilityConfig _config{};
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 어트리뷰트 하나에 주는 기본값과 범위입니다. */
    struct AbilitySetAttributeEntry
    {
        hashed_string _attribute{};
        float32       _baseValue{ 0.0f };
        float32       _minValue{ -MathUtil::kMaxFloat }; ///< `min` 을 적지 않으면 아래로 막지 않는다
        float32       _maxValue{ MathUtil::kMaxFloat };  ///< `max` 를 적지 않으면 위로 막지 않는다
        uint8         _bHasRange{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 붙일 어트리뷰트 세트 하나입니다. */
    struct AbilitySetAttributeSetEntry
    {
        hashed_string                    _className{}; ///< `registerAttributeSetClass` 로 등록한 이름("Combat", "Generic")
        vector<AbilitySetAttributeEntry> _listAttribute{};
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 줄 어빌리티 하나입니다. */
    struct AbilitySetAbilityEntry
    {
        hashed_string _abilityID{};
        int32         _level{ 1 };
        int32         _inputID{ -1 }; ///< `AbilitySystemComponent::kNoInputID` 와 같은 값
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 시작할 때 걸 이펙트 하나입니다. */
    struct AbilitySetEffectEntry
    {
        hashed_string _effectID{};
        int32         _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 오브젝트 한 종류에 한 번에 주는 어빌리티 세트입니다(언리얼 Lyra 의 `ULyraAbilitySet` 에 해당). 어트리뷰트 세트, 어빌리티, 시작 이펙트, 태그가 들어 있습니다.
     * @details 컴포넌트의 `_abilitySetID` 가 이 세트를 가리키면 플레이를 시작할 때 한 번 받는다. 플레이어와 몬스터 종류마다 하나씩 둔다.
     */
    struct AbilitySetDef
    {
        hashed_string                       _id{};
        vector<AbilitySetAttributeSetEntry> _listAttributeSet{};
        vector<AbilitySetAbilityEntry>      _listAbility{};
        vector<AbilitySetEffectEntry>       _listEffect{};
        TagContainer                        _looseTags{};
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) AbilityCatalog
    // ------------------------------------------------------------------------------
    /**
     * @class AbilityCatalog
     * @brief 이펙트, 어빌리티, 세트의 정의를 id 로 찾고, 클래스 이름으로 어빌리티, 어트리뷰트 세트, 실행 계산을 만듭니다.
     * @details **소유자**: 게임이 카탈로그를 하나 만들어 `GameInstanceBase` 의 멤버로 보관하고, 게임 서비스로 등록한다.
     *          등록은 게임 인스턴스가 `bindScopedService( &_abilityCatalog )` 로 게임 범위에 등록한다. 컴포넌트에 카탈로그를 따로 정하지 않았으면 이 서비스를 쓴다.
     *          등록한 팩토리는 게임 모듈의 코드이므로 카탈로그의 수명은 게임 모듈과 같아야 한다. 핫 리로드를 하면 새 인스턴스가 다시 등록한다.
     *
     *          **기본 등록**: 어트리뷰트 세트는 "Generic"(`AttributeSet`, 어트리뷰트를 데이터가 정함)과 "Combat"(`CombatAttributeSet`),
     *          어빌리티는 "ApplyEffects"(`ApplyEffectsAbility`), 실행 계산은 "Damage"(`DamageExecution`)이다.
     *
     *          **데이터**(`loadFromResource`, `loadFromXMLText`): 루트 `<AbilityCatalog>` 아래에 `<GameplayEffect>`, `<Ability>`, `<AbilitySet>` 을 둔다.
     *          파일 안의 순서와 상관없이 이펙트, 어빌리티, 세트 순으로 읽는다. 그래서 비용 이펙트나 쿨다운 이펙트 같은 참조가 앞에 있지 않아도 된다.
     *          여러 파일을 차례로 읽으면 합쳐지고, 같은 id 는 나중에 읽은 것이 우선한다. 형식은 `Source/GameFramework/Base/Gameplay/Ability/README.md` 에 있다.
     */
    class SW_GF_API AbilityCatalog : public XMLCatalog<AbilityCatalog>
    {
        friend class XMLCatalog<AbilityCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "abilities";

        AbilityCatalog();
        ~AbilityCatalog();

        AbilityCatalog( const AbilityCatalog& )            = delete;
        AbilityCatalog& operator=( const AbilityCatalog& ) = delete;

        // --------------------------------------------------------------------------
        // 클래스 등록. 이름과 만드는 함수를 연결한다
        // --------------------------------------------------------------------------
        void registerAbilityClass( const hashed_string& className, AbilityFactoryFunc factory );
        void registerAttributeSetClass( const hashed_string& className, AttributeSetFactoryFunc factory );
        void registerExecutionClass( const hashed_string& className, ExecutionFactoryFunc factory );
        /** @brief 어빌리티 클래스 T 를 이름으로 등록합니다. 기본 생성자로 만듭니다. */
        template <typename T>
        void registerAbilityClass( const hashed_string& className )
        {
            registerAbilityClass( className, &AbilityCatalog::createDefault<GameplayAbility, T> );
        }
        /** @brief 어트리뷰트 세트 클래스 T 를 이름으로 등록합니다. 기본 생성자로 만듭니다. */
        template <typename T>
        void registerAttributeSetClass( const hashed_string& className )
        {
            registerAttributeSetClass( className, &AbilityCatalog::createDefault<AttributeSet, T> );
        }

        // --------------------------------------------------------------------------
        // 정의. 코드로 추가한다
        // --------------------------------------------------------------------------
        /** @brief 이펙트 정의를 추가합니다. 같은 id 가 있으면 교체합니다. 이미 적용된 이펙트는 교체 전 정의를 계속 참조합니다. */
        shared_ptr<const GameplayEffectDef> addEffect( const GameplayEffectDef& def );
        /** @brief 어빌리티 정의를 추가합니다. 같은 id 가 있으면 교체합니다. */
        void addAbility( const GameplayAbilityDef& def );
        /** @brief 어빌리티 세트를 추가합니다. 같은 id 가 있으면 교체합니다. */
        void addAbilitySet( const AbilitySetDef& def );

        // --------------------------------------------------------------------------
        // 데이터. XML 로 읽는다
        // --------------------------------------------------------------------------
        /** @brief 정의를 모두 지웁니다. 클래스 등록은 그대로 둡니다. */
        void clearDefinitions();

        // --------------------------------------------------------------------------
        // 찾기와 만들기
        // --------------------------------------------------------------------------
        shared_ptr<const GameplayEffectDef> findEffect( const hashed_string& effectID ) const;
        const GameplayAbilityDef*           findAbility( const hashed_string& abilityID ) const;
        const AbilitySetDef*                findAbilitySet( const hashed_string& setID ) const;
        /** @brief 어빌리티 정의로 인스턴스를 만들고 설정을 줍니다. 정의나 클래스가 없으면 경고를 남기고 nullptr 을 반환합니다. */
        unique_ptr<GameplayAbility> createAbility( const hashed_string& abilityID ) const;
        /** @brief 등록된 이름의 어트리뷰트 세트를 만듭니다. 없으면 nullptr 입니다. */
        unique_ptr<AttributeSet> createAttributeSet( const hashed_string& className ) const;
        /** @brief 어빌리티 클래스가 등록돼 있으면 true 입니다. */
        bool hasAbilityClass( const hashed_string& className ) const;

        uint32 getEffectCount() const { return static_cast<uint32>( _mapEffect.size() ); }
        uint32 getAbilityCount() const { return static_cast<uint32>( _mapAbility.size() ); }
        uint32 getAbilitySetCount() const { return static_cast<uint32>( _mapAbilitySet.size() ); }

    private:
        template <typename TBase, typename T>
        static unique_ptr<TBase> createDefault()
        {
            return unique_ptr<TBase>( make_unique<T>() );
        }

        /** @brief `<AbilityCatalog>` 루트 하나를 읽습니다. 읽은 정의 수를 반환합니다. */
        static constexpr const utf8* kXMLRootName = "AbilityCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<GameplayEffect>` 하나를 읽습니다. */
        [[nodiscard]] bool readEffect( const XMLNode& node, string_view sourceName, GameplayEffectDef& outDef ) const;
        /** @brief `<Ability>` 하나를 읽습니다. */
        [[nodiscard]] bool readAbility( const XMLNode& node, string_view sourceName, GameplayAbilityDef& outDef ) const;
        /** @brief `<AbilitySet>` 하나를 읽습니다. */
        [[nodiscard]] bool readAbilitySet( const XMLNode& node, string_view sourceName, AbilitySetDef& outDef ) const;
        /** @brief 이펙트 id 로 정의를 찾습니다. id 가 비어 있으면 nullptr 이고, 정의가 없으면 경고를 남기고 nullptr 입니다. */
        shared_ptr<const GameplayEffectDef> resolveEffectReference( const utf8* pEffectID, const utf8* pOwnerID, string_view sourceName ) const;

        unordered_map<hashed_string, shared_ptr<const GameplayEffectDef>> _mapEffect;
        unordered_map<hashed_string, GameplayAbilityDef>                  _mapAbility;
        unordered_map<hashed_string, AbilitySetDef>                       _mapAbilitySet;
        unordered_map<hashed_string, AbilityFactoryFunc>                  _mapAbilityFactory;
        unordered_map<hashed_string, AttributeSetFactoryFunc>             _mapAttributeSetFactory;
        unordered_map<hashed_string, ExecutionFactoryFunc>                _mapExecutionFactory;
    };
} // namespace sw
