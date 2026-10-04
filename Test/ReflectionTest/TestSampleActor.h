/**
 * @file TestSampleActor.h
 * @brief Reflection 테스트용 REFLECT/ENUM 샘플 타입
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/array.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) REFLECT 샘플 — 프로퍼티·별칭·기본값·중첩
    // ------------------------------------------------------------------------------
    REFLECT()
    struct SampleTestActor
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _hp = 100;

        PROPERTY()
        string _name = "Hero";

        FUNCTION()
        /** @brief HP에서 피해량을 뺍니다. */
        void takeDamage( int32 damage )
        {
            _hp -= damage;
        }

        FUNCTION()
        /** @brief 현재 HP를 반환합니다. */
        int32 getHp() const
        {
            return _hp;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief REFLECT() 가 없는 순수 인터페이스. 다중 상속 시 프로퍼티가 없으므로 리플렉션 부모
     *        선택에서 조용히 무시되어야 합니다 (경고 없음). GameFramework::IFlagStore 축소판.
     */
    class IPlainMixinTestActor
    {
    public:
        virtual ~IPlainMixinTestActor() = default;
        virtual void mixinHook()        = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief REFLECT() 는 있지만 PROPERTY() 가 없는 베이스. GameFramework::SaveGame 축소판
     *        (프로퍼티 없는 리플렉션 베이스 — 파생 클래스가 직접 프로퍼티를 선언).
     */
    REFLECT()
    struct EmptyReflectedBaseTestActor
    {
        REFLECT_BODY();
        virtual ~EmptyReflectedBaseTestActor() = default;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief REFLECT() 베이스(EmptyReflectedBaseTestActor)가 선언 순서상 첫 번째이고, REFLECT() 가
     *        없는 순수 인터페이스가 두 번째인 다중 상속 조합 검증용 액터. GameFramework::
     *        TurnBattleSaveGame : public SaveGame, public IFlagStore 실제 사례의 축소판입니다.
     *        부모는 EmptyReflectedBaseTestActor 로 채택되어야 하고, 자신의 프로퍼티는 다중 상속과
     *        무관하게 정상 동작해야 합니다.
     */
    REFLECT()
    struct MultiBaseOrderTestActor : public EmptyReflectedBaseTestActor, public IPlainMixinTestActor
    {
        REFLECT_BODY();
        void mixinHook() override {}

        PROPERTY()
        int32 _ownValue = 7;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct AliasAndReorderTestActor
    {
        REFLECT_BODY();
        PROPERTY( Alias = "hp, HitPoints" )
        int32 _currentHp = 100;

        PROPERTY()
        int32 _score = 50;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 개명 호환 — 옛 FQN `sw::LegacyRenameActor` 로 findType / 컴포넌트 키 조회.
     */
    REFLECT( Alias = LegacyRenameActor )
    struct RenameCompatActor
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _hp = 100;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct DefaultValueTestActor
    {
        REFLECT_BODY();
        /** @brief 에셋에 _mana 가 없으면 Default 를 적용합니다(클래스 초기값 0이 아님). */
        PROPERTY( Default = "75" )
        int32 _mana{ 0 };

        PROPERTY( Default = "Apprentice", XmlAttribute )
        string _title = "unset";
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct NestedInner
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _x{ 0 };
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct NestedContainerActor
    {
        REFLECT_BODY();
        PROPERTY()
        vector<vector<int32>> _grid;

        PROPERTY()
        map<string, vector<float32>> _namedRows;

        /** @brief 중첩 맵: map<K, map<K, V>>. */
        PROPERTY()
        map<string, map<string, int32>> _nestedMap;

        /** @brief 구조체 원소 시퀀스. */
        PROPERTY()
        vector<NestedInner> _listInner;

        /** @brief 맵 값이 구조체인 경우. */
        PROPERTY()
        map<string, NestedInner> _mapInner;

        PROPERTY()
        NestedInner _inner;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) FUNCTION / RPC / Abstract / Static / 생성자
    // ------------------------------------------------------------------------------
    REFLECT()
    struct RpcDemoActor
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _hp = 100;

        FUNCTION( Server, Reliable, Category = "Combat", DisplayName = "Apply Damage",
                  Tooltip = "Subtracts amount from HP" )
        /** @brief HP에서 피해량을 뺍니다(RPC 데모). */
        void applyDamage( int32 amount )
        {
            _hp -= amount;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief Unreal UCLASS(Abstract) 스타일 — 등록되지만 생성할 수 없습니다. */
    REFLECT( Abstract )
    struct AbstractDemoBase
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _baseValue{ 1 };

        /** @brief 가상 소멸자. */
        virtual ~AbstractDemoBase() = default;
        /** @brief 추상 틱 훅. */
        virtual void tickAbstract() = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief BlueprintFunctionLibrary 스타일의 static 헬퍼. */
    REFLECT( Static )
    struct StaticDemoLibrary
    {
        REFLECT_BODY();
        FUNCTION( Category = "Math", DisplayName = "Double Int", Tooltip = "Returns value * 2" )
        /** @brief value * 2 를 반환합니다. */
        static int32 doubleInt( int32 value )
        {
            return value * 2;
        }
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct PolyPayloadA
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _a{ 1 };
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct AssetPathActor
    {
        REFLECT_BODY();
        PROPERTY( AssetPath, AssetType = "Texture" )
        string _albedo;

        PROPERTY( Polymorphic )
        ReflectAny _payload;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `Abstract = true` — 단독 토큰이 아니라 대입 형태로 적은 표본입니다.
     * @details AnnotationMeta.txt 의 `flag` 줄이 단독 토큰과 `X = true` 를 함께 등록하지 않으면 이 형태가
     *          경고 한 줄 없이 버려집니다.
     */
    REFLECT( Abstract = true )
    struct AssignedAbstractBase
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _baseValue{ 1 };

        /** @brief 가상 소멸자. */
        virtual ~AssignedAbstractBase() = default;
        /** @brief 추상 훅. */
        virtual void tickAssigned() = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief `Static = true` 대입 형태 표본입니다. */
    REFLECT( Static = true )
    struct AssignedStaticLibrary
    {
        REFLECT_BODY();
        FUNCTION()
        /** @brief value + 1 을 반환합니다. */
        static int32 increment( int32 value )
        {
            return value + 1;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 프로퍼티·함수 플래그를 전부 `X = true` 로 적은 표본입니다. */
    REFLECT()
    struct AssignedFlagActor
    {
        REFLECT_BODY();

        PROPERTY( AssetPath = true, AssetType = "Texture" )
        string _albedo;

        PROPERTY( Polymorphic = true )
        ReflectAny _payload;

        /** @brief 별칭도 두 형태를 다 받아야 합니다 — 별칭 `xmlAttribute` 가 단독 토큰으로만 등록되면 이 대입 형태가 버려집니다. */
        PROPERTY( xmlAttribute = true )
        int32 _tag{ 0 };

        FUNCTION( Server, Reliable = true, Validate = true )
        /** @brief 플래그만 보는 RPC 표본입니다. */
        void ping( int32 value )
        {
            _tag = value;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 명시적 생성자로 ReflectionParser 가 `$ctor` / `$ctor(int32)` 를 출력하게 합니다. */
    REFLECT()
    struct CtorDemoActor
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value = -1;

        /** @brief `_value` 를 0으로 두는 기본 생성자. */
        CtorDemoActor()
            : _value{ 0 }
        {
        }

        /** @brief `_value` 를 인자로 초기화합니다. */
        explicit CtorDemoActor( int32 value )
            : _value{ value }
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 코드젠이 출력하는 PROPERTY 어노테이션 메타데이터. */
    REFLECT()
    struct MetadataDemoActor
    {
        REFLECT_BODY();
        PROPERTY( Category = "Stats", DisplayName = "Hit Points", Tooltip = "Current HP", ReadOnly )
        int32 _hp = 10;
    };
} // namespace sw

namespace sw
{
    /** @brief 비트필드(uint8 : 1) 리플렉션 테스트 액터 */
    REFLECT()
    struct BitfieldTestActor
    {
        REFLECT_BODY();
        PROPERTY( Category = "Flags" )
        uint8 _bActive : 1;

        PROPERTY( Category = "Flags" )
        uint8 _bInvulnerable : 1;

        PROPERTY( Category = "Flags" )
        uint8                  _bCanJump : 1;
        [[maybe_unused]] uint8 _reserved : 5;

        PROPERTY()
        int32 _score;

        BitfieldTestActor()
            : _bActive{ SW_FALSE }
            , _bInvulnerable{ SW_FALSE }
            , _bCanJump{ SW_FALSE }
            , _reserved{ 0 }
            , _score{ 100 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @brief uint8, uint16, uint32, uint64 비트필드 플래그(: 1) 종합 리플렉션 테스트 액터 */
    REFLECT()
    struct WideBitfieldTestActor
    {
        REFLECT_BODY();
        PROPERTY( Category = "Flags16" )
        uint16 _bFlag16_A : 1;

        PROPERTY( Category = "Flags16" )
        uint16                  _bFlag16_B  : 1;
        [[maybe_unused]] uint16 _reserved16 : 14;
        [[maybe_unused]] uint16 _pad16;

        PROPERTY( Category = "Flags32" )
        uint32 _bFlag32_A : 1;

        PROPERTY( Category = "Flags32" )
        uint32                  _bFlag32_B  : 1;
        [[maybe_unused]] uint32 _reserved32 : 30;
        [[maybe_unused]] uint32 _pad32;

        PROPERTY( Category = "Flags64" )
        uint64 _bFlag64_A : 1;

        PROPERTY( Category = "Flags64" )
        uint64                  _bFlag64_B  : 1;
        [[maybe_unused]] uint64 _reserved64 : 62;

        WideBitfieldTestActor()
            : _bFlag16_A{ SW_FALSE }
            , _bFlag16_B{ SW_FALSE }
            , _reserved16{ 0 }
            , _pad16{ 0 }
            , _bFlag32_A{ SW_FALSE }
            , _bFlag32_B{ SW_FALSE }
            , _reserved32{ 0 }
            , _pad32{ 0 }
            , _bFlag64_A{ SW_FALSE }
            , _bFlag64_B{ SW_FALSE }
            , _reserved64{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) ENUM — 별칭·Flags·중첩 네임스페이스
    // ------------------------------------------------------------------------------
    ENUM( Alias = LegacySampleStatus, ValueAlias = "OldIdle:Idle, OldMoving:Moving" )
    enum class SampleStatus : uint8
    {
        Idle,
        Moving,
        Attacking,
    };

    namespace InnerNamespaceForTest
    {
        REFLECT()
        struct OuterStruct
        {
            REFLECT_BODY();
            PROPERTY()
            int32 _outerValue = 42;

            REFLECT()
            struct InnerStruct
            {
                REFLECT_BODY();
                PROPERTY()
                string _innerData = "NestedData";

                PROPERTY()
                float32 _score = 3.14f;
            };

            REFLECT()
            class InnerClass
            {
            public:
                REFLECT_BODY();
                PROPERTY()
                int64 _id = 999;
            };

            // 비트플래그는 **말해야** 한다 — 값이 1·2·4 라는 이유로 자동 감지하지 않는다.
            // 중첩 열거형이라 비트 연산자는 코드젠되지 않지만(전방 선언 불가), 등록부의 표시는
            // 이것으로 선다.
            ENUM( Flags )
            enum class InnerEnum : uint32
            {
                OptionA = 1,
                OptionB = 2,
                OptionC = 4,
            };
        };
    } // namespace InnerNamespaceForTest

    // ------------------------------------------------------------------------------
    // 4) Component 생명주기 훅·상속
    // ------------------------------------------------------------------------------
    REFLECT()
    struct TestScriptComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _scriptSpeed{ 1.5f };

        /**
         * 이 변수는 PROPERTY() 가 주석에 있지만, 실제로는 파싱되지 않아야 합니다.
         */
        int32 _shouldNotBeParsed{ 0 };

        uint32 _tickCount{ 0 };
        bool   _beganPlay{ false };
        bool   _endedPlay{ false };

        /** @brief 플레이 시작 플래그를 켭니다. */
        void onBeginPlay() override
        {
            _beganPlay = true;
        }

        /** @brief 틱 카운트를 증가시킵니다. */
        void onTick( float32 dt ) override
        {
            (void)dt;
            _tickCount++;
        }

        /** @brief 플레이 종료 플래그를 켭니다. */
        void onEndPlay() override
        {
            _endedPlay = true;
        }
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct TestDerivedScriptComponent : public TestScriptComponent
    {
        REFLECT_BODY();

        uint32 _derivedTickCount{ 0 };

        /** @brief 부모 틱 후 파생 틱 카운트를 더합니다. */
        void onTick( float32 dt ) override
        {
            TestScriptComponent::onTick( dt );
            _derivedTickCount += 2;
        }
    };
} // namespace sw

namespace sw
{
    REFLECT( Alias = LegacyGrandChildScriptComponent )
    struct TestGrandChildScriptComponent : public TestDerivedScriptComponent
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _grandChildSpeed{ 3.0f };

        uint32 _grandChildTickCount{ 0 };

        /** @brief 부모 틱 후 손자 틱 카운트를 더합니다. */
        void onTick( float32 dt ) override
        {
            TestDerivedScriptComponent::onTick( dt );
            _grandChildTickCount += 3;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 상태 묶음이 핸들을 옮기는 자리와 옮기지 못하는 자리를 한 컴포넌트에 둔 샘플입니다(`ObjectStateBatch::finish`). */
    REFLECT()
    struct TestHandleHolderComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        GameObjectHandle _target; ///< 옮긴다

        PROPERTY()
        vector<GameObjectHandle> _listTarget; ///< 옮긴다

        PROPERTY()
        map<int32, GameObjectHandle> _mapSlotToTarget; ///< 옮기지 못한다 — 파일 상태면 비운다

        PROPERTY()
        ComponentHandle _targetComponent; ///< 옮기지 못한다 — 파일 상태면 비운다
    };
} // namespace sw

namespace sw
{
    REFLECT( Category = "Gameplay", DisplayName = "Meta Test Actor", Tooltip = "Actor for testing rich metadata", HideInMenu, Meta = "CustomTag=ActorVal, Priority=10" )
    struct MetaTestActor
    {
        REFLECT_BODY();

        PROPERTY( Category = "Stats", DisplayName = "Health Points", Tooltip = "Current health", Transient, HideInInspector, Meta = "Units=HP, Clamp=True" )
        int32 _health = 100;

        PROPERTY( Category = "Stats", DisplayName = "Armor", Tooltip = "Armor rating" )
        int32 _armor = 50;

        FUNCTION( Category = "Actions", DisplayName = "Reset Health", Tooltip = "Resets health to 100", CallInEditor, Meta = "ActionType=Reset", EditorPreview = "HealthReset" )
        void resetHealth()
        {
            _health = 100;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 값이 객체 밖에 있는 프로퍼티(접근자 프로퍼티) 샘플입니다. 씬 컴포넌트의 로컬 TRS 가 트랜스폼 저장소의 칸에 사는 모양의 축소판입니다.
     * @details `_position` 은 필드처럼 붙인 프로퍼티 이름일 뿐이고, 값은 바깥 배열 `s_arrExternalPosition` 의 이 객체 칸(`_storageIndex`)에 있습니다.
     *          `_level` 은 보통 필드입니다. 둘이 한 타입에 섞여도 선언 순서대로 등록되는지, 직렬화가 두 자리를 다 따라가는지 봅니다.
     */
    REFLECT()
    struct ExternalStorageTestActor
    {
        REFLECT_BODY();

        /** @brief 값 저장소입니다. 객체마다 칸 하나를 씁니다. 정의는 `TestReflectionSerialization.cpp` 에 있습니다(헤더의 inline 정의는 모듈마다 사본이 생긴다). */
        static float3 s_arrExternalPosition[4];

        /** @brief 이 객체 칸의 위치 값 자리입니다. 리플렉션은 이 함수로 값을 찾습니다. */
        PROPERTY( Name = "_position", Category = "Transform" )
        float3& getPositionRef() { return s_arrExternalPosition[_storageIndex]; }

        PROPERTY()
        int32 _level{ 0 };

        /** @brief `s_arrExternalPosition` 에서 이 객체가 쓰는 칸입니다. 리플렉션 대상이 아닙니다. */
        uint32 _storageIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /// @brief 별칭으로 적은 컨테이너 — 파서가 벗겨서 컨테이너로 알아봐야 한다(`AliasContainerActor`).
    using TestAliasScoreList = sw::vector<int32>;
    /// @brief 별칭으로 적은 스칼라 — 이름(int32 의 별칭)이 그대로여야 한다.
    using TestAliasCount = int32;

    /**
     * @struct AliasContainerActor
     * @brief 별칭으로 적은 컨테이너 · 스칼라 프로퍼티 샘플(`ReflectionTest.AliasedContainerPropertyIsAContainer`).
     */
    REFLECT()
    struct AliasContainerActor
    {
        REFLECT_BODY();

        PROPERTY()
        TestAliasScoreList _aliasScores;

        PROPERTY()
        TestAliasCount _aliasCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 고정 배열 프로퍼티 샘플입니다(`ReflectionSerializationTest.FixedArrayPropertyRoundTripsInEveryFormat`). */
    REFLECT()
    struct FixedArrayActor
    {
        REFLECT_BODY();

        PROPERTY()
        array<int32, 3> _arrSlot{};

        PROPERTY()
        int32 _after{ 0 }; ///< 배열 뒤의 칸 — 배열을 읽다 스트림이 어긋나면 여기가 틀린다
    };
} // namespace sw

namespace sw
{
    /** @brief 이름으로 부르기 · 기본 인자 · 이벤트 샘플입니다(`ReflectionInvokeTest`). */
    REFLECT()
    struct InvokeDemoActor
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _hp = 100;

        PROPERTY()
        SampleStatus _status = SampleStatus::Idle;

        PROPERTY()
        int32 _lastReported = 0;

        PROPERTY( Category = "Events", Tooltip = "HP changed" )
        MulticastDelegate<void( int32 newHp, const string& reason )> _onHpChanged;

        PROPERTY()
        MulticastDelegate<void()> _onDied;

        /** @brief 회복량에 배율을 곱해 더하고 지금 HP 를 돌려줍니다. */
        FUNCTION()
        int32 heal( int32 amount, float32 multiplier = 1.5f )
        {
            _hp += static_cast<int32>( static_cast<float32>( amount ) * multiplier );
            return _hp;
        }

        /** @brief 상태를 바꿉니다. 인자를 빼면 Moving 입니다. */
        FUNCTION()
        void setStatus( SampleStatus status = SampleStatus::Moving )
        {
            _status = status;
        }

        /** @brief `접두어:HP` 글을 돌려줍니다. */
        FUNCTION()
        string describe( const string& prefix = "hp" ) const
        {
            return prefix + ":" + to_string( _hp );
        }

        /** @brief 이벤트에 묶이는 받는 쪽입니다. */
        FUNCTION()
        void reportHp( int32 value )
        {
            _lastReported = value;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 역할 플래그(복제 · 세이브 · 보간 · 설정) 샘플입니다(`ReflectionPropertyRoleTest`). */
    REFLECT()
    struct PropertyRoleActor
    {
        REFLECT_BODY();

        PROPERTY( RepNotify = onHealthReplicated )
        int32 _health = 100;

        PROPERTY( Replicated, RepNotify = onTeamReplicated )
        int32 _team = 0;

        PROPERTY( SaveGame )
        int32 _gold = 0;

        PROPERTY( SaveGame )
        string _heroName = "nobody";

        /** @brief 세이브 대상이 아니다 — 세이브 읽기는 이 값을 덮지도 기본값으로 되돌리지도 않는다. */
        PROPERTY( Default = "7" )
        int32 _sessionScore = 0;

        PROPERTY( Interp )
        float32 _opacity = 1.0f;

        PROPERTY( Interp )
        float3 _offset{};

        /** @brief 직렬화기는 quaternion 을 싣지 않는다 — 런타임에 섞기만 하는 값이다. */
        PROPERTY( Interp, Transient )
        quaternion _rotation = quaternion::Identity;

        PROPERTY( Interp )
        int32 _step = 0;

        PROPERTY( Config )
        float32 _volume = 0.5f;

        PROPERTY( ConfigSection = "Audio", ConfigKey = "master" )
        float32 _masterVolume = 1.0f;

        int32 _oldHealthSeen   = -1;
        int32 _teamNotifyCount = 0;

        /** @brief 체력이 복제로 바뀐 뒤 불립니다. 이전 값을 받습니다. */
        void onHealthReplicated( const int32& oldHealth ) { _oldHealthSeen = oldHealth; }

        /** @brief 팀이 복제로 바뀐 뒤 불립니다. */
        void onTeamReplicated() { ++_teamNotifyCount; }
    };
} // namespace sw

namespace sw
{
    /** @brief 표시 메타 샘플의 모드입니다. */
    ENUM()
    enum class DisplayMetaMode : uint8
    {
        Off,
        Orbit,
        Follow,
    };

    /** @brief 표시 메타(EditCondition · 단위 · 슬라이더 범위 · HDR 색 · 여러 줄 · 파일 필터 · C 고정 배열) 샘플입니다(`ReflectionDisplayMetaTest`). */
    REFLECT()
    struct DisplayMetaActor
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled = false;

        PROPERTY()
        DisplayMetaMode _mode = DisplayMetaMode::Off;

        PROPERTY( EditCondition = "_bEnabled" )
        float32 _speed = 1.0f;

        PROPERTY( EditCondition = "!_bEnabled", EditConditionHides )
        int32 _fallback = 0;

        PROPERTY( EditCondition = "_mode == Orbit" )
        float32 _orbitRadius = 5.0f;

        PROPERTY( EditCondition = "_mode != DisplayMetaMode::Off" )
        float32 _blend = 0.5f;

        PROPERTY( Units = cm, Min = 0, Max = 1000, UiMin = 50, UiMax = 250 )
        float32 _height = 180.0f;

        PROPERTY( ColorHdr )
        float3 _emissive{};

        PROPERTY( Multiline )
        string _notes;

        PROPERTY( AssetPath, FileFilter = "*.png;*.dds" )
        string _texture;

        PROPERTY()
        int32 _arrSlot[3] = { 1, 2, 3 };

        PROPERTY()
        float3 _arrPoint[2]{};

        PROPERTY()
        int32 _after = 0; ///< 배열 뒤의 칸 — 배열을 읽다 스트림이 어긋나면 여기가 틀린다
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 샘플 — 값으로 들리는 구조체입니다(`ReflectionValidationTest`). */
    REFLECT()
    struct ValidatedPart
    {
        REFLECT_BODY();

        PROPERTY( Validate = validateWeight )
        float32 _weight = 1.0f;

        /** @brief 무게는 음수가 아니다. */
        void validateWeight( ValidationContext& context ) const
        {
            if ( _weight < 0.0f )
                context.addError( "weight is negative" );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 샘플 — 프로퍼티 검증 · 타입 검증 · 값 구조체 · 그 시퀀스입니다(`ReflectionValidationTest`). */
    REFLECT( Validate = validateRange )
    struct ValidatedActor
    {
        REFLECT_BODY();

        PROPERTY( Validate = validateName )
        string _name = "ok";

        PROPERTY()
        int32 _min = 0;

        PROPERTY()
        int32 _max = 10;

        PROPERTY()
        ValidatedPart _part;

        PROPERTY()
        vector<ValidatedPart> _listPart;

        /** @brief 이름은 비지 않는다(경고). */
        void validateName( ValidationContext& context ) const
        {
            if ( _name.empty() )
                context.addWarning( "name is empty" );
        }

        /** @brief 아래 경계가 위 경계를 넘지 않는다. const 가 아니어도 된다. */
        void validateRange( ValidationContext& context )
        {
            if ( _max < _min )
                context.addError( "min is greater than max" );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 함수는 상속된다 — 이 타입은 자기 것이 없어도 기반의 것이 돈다. */
    REFLECT()
    struct ValidatedChildActor : public ValidatedActor
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _extra = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 함수가 있는 컴포넌트입니다 — 오브젝트 로드 · 저장 · 인스펙터 편집 길을 본다(`ReflectionValidationTest`). */
    REFLECT( Validate = validateSpeed )
    struct ValidatedComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _speed = 1.0f;

        /** @brief 속도는 0 보다 크다. */
        void validateSpeed( ValidationContext& context ) const
        {
            if ( _speed <= 0.0f )
                context.addError( "speed must be positive" );
        }
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 5) ENUM Flags — 전역 비트플래그 샘플
// ------------------------------------------------------------------------------
ENUM( Flags )
enum class TestFlag : uint8
{
    None    = 0,
    Read    = SW_BIT( 0 ),
    Write   = SW_BIT( 1 ),
    Execute = SW_BIT( 2 )
};

ENUM( Meta = "Doc=EnumForTesting, Version=2" )
enum class TestMetaEnum : uint32
{
    First  = 0,
    Second = 1
};

// ------------------------------------------------------------------------------
// 6) 좁은 enum 의 부호 — 값의 생성(코드젠)과 읽기(런타임)가 같은 부호 규칙이어야 한다
// ------------------------------------------------------------------------------
/** @brief 부호 없는 1 바이트의 높은 비트. 코드젠이 부호 있게 적으면 0x80 이 -128 이 된다. */
ENUM()
enum class TestHighBitEnum : uint8
{
    Low  = 1,
    High = 0x80,
    Max  = 0xFF
};

/** @brief 부호 있는 1 바이트의 음수. 읽기가 0 확장하면 -1 이 255 가 된다. */
ENUM()
enum class TestSignedNarrowEnum : int8
{
    Negative = -1,
    Zero     = 0,
    Positive = 1
};
