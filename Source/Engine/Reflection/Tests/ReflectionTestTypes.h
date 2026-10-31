/**
 * @file ReflectionTestTypes.h
 * @brief 리플렉션 단위 테스트(`Engine/Reflection/Tests`)가 쓰는 반사 타입입니다 — 코드젠을 거쳐 Engine 모듈에 오른다.
 * @details 개발 구성만 코드젠한다(배포 구성은 `Tests/` 의 반사 헤더를 훑지 않는다 — cmake/Engine/ReflectionCodeGen.cmake). 컴포넌트가 아닌 값 타입만 둔다
 *          (컴포넌트는 "Add Component" 메뉴에 오르고, 이 층(1)은 컴포넌트 모델(5)을 include 하지 못한다).
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    /** @brief 이름으로 부르기 · 문서 · 열거형 인자 샘플의 상태입니다. */
    ENUM()
    enum class SampleStatus : uint8
    {
        Idle,
        Moving,
        Attacking,
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
