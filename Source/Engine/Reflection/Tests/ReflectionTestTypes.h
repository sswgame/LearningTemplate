/**
 * @file ReflectionTestTypes.h
 * @brief 리플렉션 단위 테스트(`Engine/Reflection/Tests`)가 쓰는 반사 타입입니다 — 코드젠을 거쳐 Engine 모듈에 오른다.
 * @details 개발 구성만 코드젠한다(배포 구성은 `Tests/` 의 반사 헤더를 훑지 않는다 — cmake/Modules/Build/ReflectionCodeGen.cmake). 컴포넌트가 아닌 값 타입만 둔다
 *          (컴포넌트는 "Add Component" 메뉴에 오르고, 이 층(1)은 컴포넌트 모델(5)을 include 하지 못한다).
 */
#pragma once
#include "Core/Container/array.h"
#include "Core/Container/map.h"
#include "Core/Container/set.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectedAny.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    /** @brief 이름으로 부르기 · 문서 · 열거형 인자 샘플의 상태입니다. 타입 별칭 · 열거자 별칭(옛 이름 읽기)도 이 타입으로 본다. */
    ENUM( Alias = LegacySampleStatus, ValueAlias = "OldIdle:Idle, OldMoving:Moving" )
    enum class SampleStatus : uint8
    {
        Idle,
        Moving,
        Attacking,
    };

    /** @brief 이름 표 조회 샘플입니다(`ReflectionEnumInfoTest`). 옛 테스트는 손으로 등록했다 — 지금은 코드젠이 등록한다. */
    ENUM()
    enum class DummyType : int64
    {
        None  = 0,
        TypeA = 1,
        TypeB = 2,
    };

    /** @brief 넓은(int64) 비트플래그 샘플입니다(`ReflectionEnumBitFlagTest`). */
    ENUM( Flags )
    enum class DummyBitFlag : int64
    {
        None    = 0,
        OptionA = 1,
        OptionB = 2,
        OptionC = 4,
    };

    /** @brief 좁은(uint8) 비트플래그 샘플입니다 — 코드젠 연산자와 `EnumUtil` 이 함께 도는지 본다(`ReflectionEnumFlagTest`). */
    ENUM( Flags )
    enum class TestFlag : uint8
    {
        None    = 0,
        Read    = 1,
        Write   = 2,
        Execute = 4,
    };

    /** @brief 부호 없는 1 바이트의 높은 비트입니다. 코드젠이 부호 있게 적으면 0x80 이 -128 이 된다. */
    ENUM()
    enum class TestHighBitEnum : uint8
    {
        Low  = 1,
        High = 0x80,
        Max  = 0xFF,
    };

    /** @brief 부호 있는 1 바이트의 음수입니다. 읽기가 0 확장하면 -1 이 255 가 된다. */
    ENUM()
    enum class TestSignedNarrowEnum : int8
    {
        Negative = -1,
        Zero     = 0,
        Positive = 1,
    };

    /** @brief uint8 폭 enum 입니다 — 역직렬화가 실제 크기만큼만 써야 인접 필드가 깨지지 않는다. */
    ENUM()
    enum class NarrowEnum : uint8
    {
        Zero = 0,
        One  = 1,
        Two  = 2,
    };
} // namespace sw

namespace sw
{
    /** @brief `NarrowEnum` 뒤에 반사하지 않는 1 바이트 필드를 두어 enum 읽기의 초과 기록을 잡는다. */
    REFLECT()
    struct NarrowEnumHost
    {
        REFLECT_BODY();

        PROPERTY()
        NarrowEnum _mode{ NarrowEnum::Zero };

        uint8 _guard{ 0xAB };
        uint8 _guard2{ 0xCD };
        uint8 _guard3{ 0xEF };
    };
} // namespace sw

namespace sw
{
    /** @brief 역할 플래그 · 검증 함수가 하나도 없는 평범한 샘플입니다. */
    REFLECT()
    struct SampleTestActor
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _hp = 100;

        PROPERTY()
        string _name = "Hero";

        /** @brief HP 에서 피해량을 뺍니다. */
        FUNCTION()
        void takeDamage( int32 damage ) { _hp -= damage; }

        /** @brief 지금 HP 입니다. */
        FUNCTION()
        int32 getHp() const { return _hp; }
    };
} // namespace sw

namespace sw
{
    /** @brief 이름으로 부르기 · 기본 인자 · 이벤트 샘플입니다(`ReflectionInvokeTest` · `ReflectionDocWriterTest`). */
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
        void setStatus( SampleStatus status = SampleStatus::Moving ) { _status = status; }

        /** @brief `접두어:HP` 글을 돌려줍니다. */
        FUNCTION()
        string describe( const string& prefix = "hp" ) const { return prefix + ":" + to_string( _hp ); }

        /** @brief 이벤트에 묶이는 받는 쪽입니다. */
        FUNCTION()
        void reportHp( int32 value ) { _lastReported = value; }
    };
} // namespace sw

namespace sw
{
    /** @brief 역할 플래그(복제 · 세이브 · 보간) 샘플입니다(`ReflectionPropertyRoleTest`). */
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

        PROPERTY()
        float32 _volume = 0.5f;

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
    /** @brief 저장 목적 표의 네 행(플래그 없음 · Transient · RuntimeState · SaveGame 과 RuntimeState)을 가진 옵트인 세이브 샘플입니다(`ReflectionPurposeTest`). */
    REFLECT()
    struct PurposeOptInSample
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _plain = 0;

        PROPERTY( Transient )
        int32 _transient = 0;

        PROPERTY( RuntimeState )
        int32 _runtime = 0;

        PROPERTY( SaveGame )
        int32 _saved = 0;

        PROPERTY( RuntimeState, SaveGame )
        int32 _savedRuntime = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief `SaveGame` 필드가 없는(옵트인이 아닌) 저장 목적 샘플입니다 — 세이브는 Transient 밖을 모두 씁니다(`ReflectionPurposeTest`). */
    REFLECT()
    struct PurposePlainSample
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _plain = 0;

        PROPERTY( Transient )
        int32 _transient = 0;

        PROPERTY( RuntimeState )
        int32 _runtime = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 원소가 `PurposePlainSample` 인 목록을 든 샘플입니다 — 원소 안의 RuntimeState 필드가 바깥 읽기에 어떻게 올라오는지 봅니다(`ReflectionPurposeTest`). */
    REFLECT()
    struct PurposeListHolder
    {
        REFLECT_BODY();

        PROPERTY()
        vector<PurposePlainSample> _listSample;

        PROPERTY()
        int32 _after = 0; ///< 목록 뒤의 칸 — 목록을 읽다 어긋나면 여기가 틀린다
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

    /**
     * @brief 표시 메타(EditCondition · 단위 · 슬라이더 범위 · HDR 색 · 여러 줄 · 파일 필터 · C 고정 배열) 샘플입니다(`ReflectionDisplayMetaTest`).
     * @details 편집 조건 판정은 편집기 단위 테스트가 본다 — 그래서 내보낸다(편집기 모듈이 `StaticType()` 을 부른다).
     */
    REFLECT()
    struct SW_API DisplayMetaActor
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled = SW_FALSE;

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

        PROPERTY( Units = cm, Min = 0, Max = 1000, UIMin = 50, UIMax = 250 )
        float32 _height = 180.0f;

        PROPERTY( ColorHDR )
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
    /** @brief 부모 사슬 · `isDerivedFrom` 샘플의 바탕입니다(`ReflectionTypeInfoTest`). 옛 테스트는 손으로 등록했다 — 지금은 코드젠이 등록한다. */
    REFLECT()
    struct DummyBase
    {
        REFLECT_BODY();
    };
} // namespace sw

namespace sw
{
    /** @brief 프로퍼티 셋을 가진 `DummyBase` 의 자식입니다(`ReflectionTypeInfoTest` · `ReflectionPropertyInfoTest`). */
    REFLECT()
    struct DummyActor : public DummyBase
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _hp{ 0 };

        PROPERTY()
        string _name = "";

        PROPERTY()
        float32 _speed{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 스칼라 · 비트플래그 · 시퀀스 · 맵을 고루 가진 직렬화 샘플입니다(`ReflectionSerializationTest`). */
    REFLECT()
    struct ComplexData
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _id = 101;

        PROPERTY()
        string _title = "HeroData";

        PROPERTY()
        DummyBitFlag _flags = static_cast<DummyBitFlag>( static_cast<int64>( DummyBitFlag::OptionA ) | static_cast<int64>( DummyBitFlag::OptionC ) );

        PROPERTY()
        vector<int32> _listScore;

        PROPERTY()
        map<string, int32> _mapStat;
    };
} // namespace sw

namespace sw
{
    /** @brief 메타(`Meta = "…"`)가 붙은 열거형입니다(`ReflectionMetadataTest`). */
    ENUM( Meta = "Doc=EnumForTesting, Version=2" )
    enum class TestMetaEnum : uint32
    {
        First  = 0,
        Second = 1,
    };
} // namespace sw

namespace sw
{
    /** @brief 프로퍼티 별칭(옛 키 읽기)과 필드 순서 바뀜 샘플입니다. */
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
    /** @brief 타입 개명 호환 — 옛 FQN `sw::LegacyRenameActor` 로 `findType` 한다. */
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
    /** @brief 파일에 없는 필드는 `Default` 를 쓴다(클래스 초기값이 아니라) — 샘플입니다. */
    REFLECT()
    struct DefaultValueTestActor
    {
        REFLECT_BODY();

        /** @brief 에셋에 _mana 가 없으면 Default 를 적용합니다(클래스 초기값 0이 아님). */
        PROPERTY( Default = "75" )
        int32 _mana{ 0 };

        PROPERTY( Default = "Apprentice", XMLAttribute )
        string _title = "unset";
    };
} // namespace sw

namespace sw
{
    /** @brief 중첩 컨테이너의 원소 구조체입니다. */
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
    /** @brief 중첩 컨테이너(시퀀스의 시퀀스 · 맵의 시퀀스 · 맵의 맵 · 구조체 원소) 샘플입니다. */
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
    /** @brief RPC 함수 샘플입니다. */
    REFLECT()
    struct RPCDemoActor
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _hp = 100;

        /** @brief HP 에서 피해량을 뺍니다(RPC 데모). */
        FUNCTION( Server, Reliable, Category = "Combat", DisplayName = "Apply Damage", Tooltip = "Subtracts amount from HP" )
        void applyDamage( int32 amount ) { _hp -= amount; }
    };
} // namespace sw

namespace sw
{
    /** @brief 언리얼 UCLASS(Abstract) 꼴 — 등록되지만 만들 수 없습니다. */
    REFLECT( Abstract )
    struct AbstractDemoBase
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _baseValue{ 1 };

        /** @brief 가상 소멸자. */
        virtual ~AbstractDemoBase() = default;
        /** @brief 추상 틱 훅. */
        virtual void updateAbstract() = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief BlueprintFunctionLibrary 꼴의 static 도우미입니다. */
    REFLECT( Static )
    struct StaticDemoLibrary
    {
        REFLECT_BODY();

        /** @brief value * 2 를 돌려줍니다. */
        FUNCTION( Category = "Math", DisplayName = "Double Int", Tooltip = "Returns value * 2" )
        static int32 doubleInt( int32 value ) { return value * 2; }
    };
} // namespace sw

namespace sw
{
    /** @brief `ReflectedAny` 다형 칸에 담기는 값입니다. */
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
    /** @brief 에셋 경로 · 다형 값 프로퍼티 샘플입니다. */
    REFLECT()
    struct AssetPathActor
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, AssetType = "Texture" )
        string _albedo;

        PROPERTY( Polymorphic )
        ReflectedAny _payload;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `Abstract = true` — 단독 토큰이 아니라 대입 꼴로 적은 표본입니다.
     * @details AnnotationMeta.txt 의 `flag` 줄이 단독 토큰과 `X = true` 를 함께 등록하지 않으면 이 꼴이 경고 한 줄 없이 버려집니다.
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
        virtual void updateAssigned() = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief `Static = true` 대입 꼴 표본입니다. */
    REFLECT( Static = true )
    struct AssignedStaticLibrary
    {
        REFLECT_BODY();

        /** @brief value + 1 을 돌려줍니다. */
        FUNCTION()
        static int32 increment( int32 value ) { return value + 1; }
    };
} // namespace sw

namespace sw
{
    /** @brief 프로퍼티 · 함수 플래그를 모두 `X = true` 로 적은 표본입니다. */
    REFLECT()
    struct AssignedFlagActor
    {
        REFLECT_BODY();

        PROPERTY( AssetPath = true, AssetType = "Texture" )
        string _albedo;

        PROPERTY( Polymorphic = true )
        ReflectedAny _payload;

        /** @brief 별칭도 두 꼴을 다 받아야 합니다 — 별칭 `xmlAttribute` 가 단독 토큰으로만 등록되면 이 대입 꼴이 버려집니다. */
        PROPERTY( xmlAttribute = true )
        int32 _tag{ 0 };

        /** @brief 플래그만 보는 RPC 표본입니다. */
        FUNCTION( Server, Reliable = true, Validate = true )
        void ping( int32 value ) { _tag = value; }
    };
} // namespace sw

namespace sw
{
    /** @brief 명시적 생성자 — 코드젠이 `$ctor` / `$ctor(int32)` 를 낸다. */
    REFLECT()
    struct CtorDemoActor
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _value = -1;

        /** @brief `_value` 를 0 으로 두는 기본 생성자. */
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
    /** @brief 코드젠이 내는 PROPERTY 어노테이션 메타 샘플입니다. */
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
    /** @brief 비트필드(uint8 : 1) 샘플입니다. */
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
    /** @brief uint16 · uint32 · uint64 비트필드 플래그(: 1) 샘플입니다. */
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
    namespace InnerNamespaceForTest
    {
        /** @brief 이름공간 안의 바깥 구조체 · 안쪽 구조체 · 안쪽 클래스 · 안쪽 열거형 샘플입니다(`ReflectionInnerTypesTest`). */
        REFLECT()
        struct OuterStruct
        {
            REFLECT_BODY();

            PROPERTY()
            int32 _outerValue = 42;

            /** @brief 안쪽 구조체입니다. */
            REFLECT()
            struct InnerStruct
            {
                REFLECT_BODY();

                PROPERTY()
                string _innerData = "NestedData";

                PROPERTY()
                float32 _score = 3.14f;
            };

            /** @brief 안쪽 클래스입니다. */
            REFLECT()
            class InnerClass
            {
            public:
                REFLECT_BODY();

                PROPERTY()
                int64 _id = 999;
            };

            // 비트플래그는 **말해야** 한다 — 값이 1·2·4 라는 이유로 자동 감지하지 않는다. 중첩 열거형이라 비트 연산자는 코드젠되지 않는다(전방 선언 불가).
            ENUM( Flags )
            enum class InnerEnum : uint32
            {
                OptionA = 1,
                OptionB = 2,
                OptionC = 4,
            };
        };
    } // namespace InnerNamespaceForTest
} // namespace sw

namespace sw
{
    /** @brief 직렬화가 덜 다루던 컨테이너 모양 — set · 정수 · 열거형 키 맵 · 맵 안의 set · 시퀀스 안의 맵 · 세 겹(`ReflectionSerializationTest`). */
    REFLECT()
    struct ContainerShapeActor
    {
        REFLECT_BODY();

        PROPERTY()
        set<string> _uniqueTag;

        PROPERTY()
        unordered_set<int32> _uniqueID;

        PROPERTY()
        unordered_map<int32, string> _mapIDToName;

        PROPERTY()
        map<SampleStatus, int32> _mapStatusToCount;

        PROPERTY()
        map<string, set<int32>> _mapGroupToID;

        PROPERTY()
        vector<map<string, int32>> _listScoreTable;

        PROPERTY()
        unordered_map<string, vector<vector<int32>>> _mapNameToGrid;

        PROPERTY()
        int32 _after = 0; ///< 컨테이너 뒤의 칸 — 컨테이너를 읽다 스트림이 어긋나면 여기가 틀린다
    };
} // namespace sw

namespace sw
{
    /** @brief 타입 · 프로퍼티 · 함수의 풍부한 메타 샘플입니다(`ReflectionMetadataTest`). */
    REFLECT( Category = "Gameplay", DisplayName = "Meta Test Actor", Tooltip = "Actor for testing rich metadata", HideInMenu, Meta = "CustomTag=ActorVal, Priority=10" )
    struct MetaTestActor
    {
        REFLECT_BODY();

        PROPERTY( Category = "Stats", DisplayName = "Health Points", Tooltip = "Current health", Transient, HideInInspector, Meta = "Units=HP, Clamp=True" )
        int32 _health = 100;

        PROPERTY( Category = "Stats", DisplayName = "Armor", Tooltip = "Armor rating" )
        int32 _armor = 50;

        /** @brief 체력을 100 으로 되돌립니다. */
        FUNCTION( Category = "Actions", DisplayName = "Reset Health", Tooltip = "Resets health to 100", CallInEditor, Meta = "ActionType=Reset", EditorPreview = "HealthReset" )
        void resetHealth() { _health = 100; }
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

        /** @brief 값 저장소입니다. 객체마다 칸 하나를 씁니다(헤더의 inline 정의는 모듈마다 사본이 생겨 직렬화 테스트 TU 에 정의한다). */
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
    /// @brief 컨테이너 원소로 적은 별칭 — 원소 이름도 벗겨 `uint64` 여야 한다(벗기지 않으면 자기 기술형 바이너리가 원소를 읽지 못한다).
    using TestAliasElementID = uint64;

    /** @brief 별칭으로 적은 컨테이너 · 스칼라 · 원소 프로퍼티 샘플입니다(`ReflectionTest.AliasedContainerPropertyIsAContainer`). */
    REFLECT()
    struct AliasContainerActor
    {
        REFLECT_BODY();

        PROPERTY()
        TestAliasScoreList _aliasScores;

        PROPERTY()
        TestAliasCount _aliasCount{ 0 };

        PROPERTY()
        sw::vector<TestAliasElementID> _listAliasID;
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
