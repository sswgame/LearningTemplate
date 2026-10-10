/**
 * @file AbilityCatalog.h
 * @brief id 로 찾는 이펙트 · 어빌리티 · 어빌리티 세트 정의와, 이름으로 만드는 클래스 등록부입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
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
    /** @brief 어트리뷰트 묶음 클래스 하나를 만드는 함수입니다(`AbilityCatalog::registerAttributeSetClass`). */
    using AttributeSetFactoryFunc = unique_ptr<AttributeSet> ( * )();
    /** @brief XML `<Execution class="...">` 노드로 실행 계산을 만드는 함수입니다(`AbilityCatalog::registerExecutionClass`). */
    using ExecutionFactoryFunc = shared_ptr<const IGameplayEffectExecution> ( * )( const XMLNode& node );

    // ------------------------------------------------------------------------------
    // 1) 정의 — 카탈로그가 들고 있는 데이터
    // ------------------------------------------------------------------------------
    /** @brief 어빌리티 하나의 정의입니다 — 어느 클래스로 만들고 어떤 설정을 줄지. */
    struct GameplayAbilityDef
    {
        hashed_string         _id{};
        hashed_string         _className{}; ///< `registerAbilityClass` 로 등록한 이름
        GameplayAbilityConfig _config{};
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 어트리뷰트 하나에 주는 기본값(과 범위)입니다. */
    struct AbilitySetAttributeEntry
    {
        hashed_string _attribute{};
        float32       _baseValue{ 0.0f };
        float32       _minValue{ 0.0f };
        float32       _maxValue{ 0.0f };
        uint8         _bHasRange{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 세트가 붙일 어트리뷰트 묶음 하나입니다. */
    struct AbilitySetAttributeSetEntry
    {
        hashed_string                    _className{}; ///< `registerAttributeSetClass` 로 등록한 이름("Combat" · "Generic")
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
    /** @brief 세트가 시작에 걸 이펙트 하나입니다. */
    struct AbilitySetEffectEntry
    {
        hashed_string _effectID{};
        int32         _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 오브젝트 한 종에 한 번에 주는 묶음입니다(언리얼 Lyra `ULyraAbilitySet`) — 어트리뷰트 묶음 · 어빌리티 · 시작 이펙트 · 태그.
     * @details 컴포넌트의 `_abilitySetID` 가 이것을 가리키면 플레이 시작에 한 번 받습니다. 플레이어 · 몬스터 종마다 하나씩 둡니다.
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
     * @brief 이펙트 · 어빌리티 · 세트 정의를 id 로 찾고, 클래스 이름으로 어빌리티 · 어트리뷰트 묶음 · 실행 계산을 만듭니다.
     * @details **누가 들고 있나**: 게임이 하나 만들어 들고(`GameInstanceBase` 의 멤버) 게임 서비스로 겁니다 —
     *          `game::bindLocalService<AbilityCatalog>( &_abilityCatalog )`. 컴포넌트는 따로 정하지 않았으면 그 서비스를 씁니다.
     *          등록한 팩토리는 게임 모듈의 코드이므로 카탈로그는 게임 모듈과 수명이 같아야 합니다(핫 리로드는 새 인스턴스가 다시 등록한다).
     *
     *          **기본 등록**: 어트리뷰트 묶음 "Generic"(`AttributeSet` — 데이터가 어트리뷰트를 정한다) · "Combat"(`CombatAttributeSet`),
     *          어빌리티 "ApplyEffects"(`ApplyEffectsAbility`), 실행 계산 "Damage"(`DamageExecution`).
     *
     *          **데이터**(`loadFromResource` · `loadFromXMLText`): 루트 `<AbilityCatalog>` 아래 `<GameplayEffect>` · `<Ability>` · `<AbilitySet>`.
     *          파일 안의 순서와 상관없이 이펙트 → 어빌리티 → 세트 순으로 읽어, 참조(비용 · 쿨다운 이펙트)가 앞에 와야 할 필요가 없습니다. 여러 파일을
     *          차례로 읽으면 합쳐지고 같은 id 는 뒤의 것이 이깁니다. 형식은 `Source/GameFramework/Base/Gameplay/Ability/README.md` 에 있습니다.
     */
    class SW_GF_API AbilityCatalog : public XMLCatalog<AbilityCatalog>
    {
        friend class XMLCatalog<AbilityCatalog>;

    public:
        AbilityCatalog();
        ~AbilityCatalog();

        AbilityCatalog( const AbilityCatalog& )            = delete;
        AbilityCatalog& operator=( const AbilityCatalog& ) = delete;

        // --------------------------------------------------------------------------
        // 클래스 등록 — 이름 → 만드는 함수
        // --------------------------------------------------------------------------
        void registerAbilityClass( const hashed_string& className, AbilityFactoryFunc factory );
        void registerAttributeSetClass( const hashed_string& className, AttributeSetFactoryFunc factory );
        void registerExecutionClass( const hashed_string& className, ExecutionFactoryFunc factory );
        /** @brief 어빌리티 클래스 T 를 이름으로 등록합니다(기본 생성). */
        template <typename T>
        void registerAbilityClass( const hashed_string& className )
        {
            registerAbilityClass( className, &AbilityCatalog::createDefault<GameplayAbility, T> );
        }
        /** @brief 어트리뷰트 묶음 클래스 T 를 이름으로 등록합니다(기본 생성). */
        template <typename T>
        void registerAttributeSetClass( const hashed_string& className )
        {
            registerAttributeSetClass( className, &AbilityCatalog::createDefault<AttributeSet, T> );
        }

        // --------------------------------------------------------------------------
        // 정의 — 코드로 더하기
        // --------------------------------------------------------------------------
        /** @brief 이펙트 정의를 더합니다(같은 id 는 바꾼다). 걸린 이펙트는 옛 정의를 계속 붙든다. */
        shared_ptr<const GameplayEffectDef> addEffect( const GameplayEffectDef& def );
        /** @brief 어빌리티 정의를 더합니다(같은 id 는 바꾼다). */
        void addAbility( const GameplayAbilityDef& def );
        /** @brief 어빌리티 세트를 더합니다(같은 id 는 바꾼다). */
        void addAbilitySet( const AbilitySetDef& def );

        // --------------------------------------------------------------------------
        // 데이터 — XML
        // --------------------------------------------------------------------------
        /** @brief 정의를 모두 지웁니다. 클래스 등록은 남깁니다. */
        void clearDefinitions();

        // --------------------------------------------------------------------------
        // 찾기 · 만들기
        // --------------------------------------------------------------------------
        shared_ptr<const GameplayEffectDef> findEffect( const hashed_string& effectID ) const;
        const GameplayAbilityDef*           findAbility( const hashed_string& abilityID ) const;
        const AbilitySetDef*                findAbilitySet( const hashed_string& setID ) const;
        /** @brief 어빌리티 정의로 인스턴스를 만들고 설정을 줍니다. 정의 · 클래스가 없으면 nullptr 입니다(경고). */
        unique_ptr<GameplayAbility> createAbility( const hashed_string& abilityID ) const;
        /** @brief 등록된 이름의 어트리뷰트 묶음을 만듭니다. 없으면 nullptr 입니다. */
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

        /** @brief `<AbilityCatalog>` 루트 하나를 읽습니다. 읽은 정의 수를 돌려줍니다. */
        static constexpr const utf8* kXMLRootName = "AbilityCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<GameplayEffect>` 하나를 읽습니다. */
        [[nodiscard]] bool readEffect( const XMLNode& node, string_view sourceName, GameplayEffectDef& outDef ) const;
        /** @brief `<Ability>` 하나를 읽습니다. */
        [[nodiscard]] bool readAbility( const XMLNode& node, string_view sourceName, GameplayAbilityDef& outDef ) const;
        /** @brief `<AbilitySet>` 하나를 읽습니다. */
        [[nodiscard]] bool readAbilitySet( const XMLNode& node, string_view sourceName, AbilitySetDef& outDef ) const;
        /** @brief 이펙트 id 를 정의로 풉니다. 비었으면 nullptr, 없으면 경고하고 nullptr 입니다. */
        shared_ptr<const GameplayEffectDef> resolveEffectReference( const utf8* pEffectID, const utf8* pOwnerID, string_view sourceName ) const;

        unordered_map<hashed_string, shared_ptr<const GameplayEffectDef>> _mapEffect;
        unordered_map<hashed_string, GameplayAbilityDef>                  _mapAbility;
        unordered_map<hashed_string, AbilitySetDef>                       _mapAbilitySet;
        unordered_map<hashed_string, AbilityFactoryFunc>                  _mapAbilityFactory;
        unordered_map<hashed_string, AttributeSetFactoryFunc>             _mapAttributeSetFactory;
        unordered_map<hashed_string, ExecutionFactoryFunc>                _mapExecutionFactory;
    };
} // namespace sw
