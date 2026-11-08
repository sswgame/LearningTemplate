/**
 * @file SequenceAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 시퀀서 타임라인 XML 에셋입니다.
 * @details **바인딩**: 항목과 키 트랙은 씬 오브젝트를 이름이 아니라 에셋 안의 바인딩 id(`SequenceBinding::_id`)로 가리킨다. 어느 오브젝트가 그 id 인지는
 *          시퀀스를 재생하는 쪽(씬의 `SequencePlayerComponent::_listBinding`)이 정한다 — 유니티 Timeline 의 에셋 · PlayableDirector 바인딩 나눔과 같다.
 *          그래서 같은 시퀀스를 다른 씬 · 다른 캐릭터에 다시 쓰고, 오브젝트 이름을 바꿔도 연결이 끊기지 않는다. 바인딩 이름은 표시용이다(찾는 데 쓰지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Curve/FloatCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;
    struct SequenceSpanHandler;
} // namespace sw

namespace sw
{
    /**
     * @brief 프레임 번호가 가질 수 있는 절댓값 상한입니다.
     * @details 프레임 번호는 파일에서 오므로 그대로 믿으면 안 된다. 타임라인 코드는 두 프레임의
     *          **차**를 그냥 빼기로 구하는데(`_frameMax - _frameMin`, `_end - _start`), int32 의
     *          양 끝값이 쌍으로 들어오면 그 빼기가 부호 있는 오버플로(UB)가 된다. 범위를 절반으로
     *          묶으면 어떤 두 값의 차도 반드시 int32 안에 들어오므로, 빼는 곳마다 넓은 타입으로
     *          올리지 않아도 된다. 30fps 기준 1,000만 일이 넘는 길이라 실사용을 막지 않는다. 밖의 값은 읽기 오류다.
     */
    constexpr int32 kSequenceFrameLimit = MathUtil::kMaxInt32 / 2;
    /** @brief 바인딩이 없음을 뜻하는 id 입니다. 바인딩 id 는 1 부터 줍니다. */
    constexpr uint32 kSequenceNoBinding = 0;

    /**
     * @brief 시퀀서 트랙 항목의 종류입니다. 파일에는 열거자 이름(`"CameraCut"`)으로 적힙니다. 이름을 바꾸면 데이터를 다시 씁니다.
     * @details 종류를 추가하면 값 하나와 종류 표(`SequenceAsset.cpp` 의 `kArrSequenceItemKindInfo`)의 줄 하나를 추가한다(static_assert 가 알려 준다).
     *          줄 하나가 그 종류의 칸(필수 · 허용) · 재생 동작(역할 깃발 또는 구간 처리기 `SequenceSpanHandler`) · 에디터 색을 모두 정한다 — 에디터 칸 · 파일 검사 · 재생이 같은 줄을 본다.
     */
    ENUM()
    enum class SequenceItemKind : int32
    {
        Clip        = 0, /**< 구간 동안 대상을 켭니다(유니티 Timeline 의 Activation 트랙). 움직임은 키 트랙(`SequenceKeyTrack`)이 정합니다. */
        Event       = 1, /**< 시작 프레임을 지날 때 한 번 알립니다(`SequencePlayerComponent::registerSequenceEvent`). */
        CameraCut   = 2, /**< 구간 동안 대상의 카메라가 게임 카메라입니다(언리얼 Camera Cut 트랙, `CameraRegistry::setCutCamera`). `_preset` 이 있으면 대상의 카메라 디렉터가 그 프리셋을 켭니다. */
        CameraShake = 3, /**< 구간 동안 대상 카메라를 흔듭니다(언리얼 Camera Shake 트랙). 세기는 `_amplitude`, 길이는 구간입니다. */
        Spawn       = 4, /**< 프리팹 `_prefab` 을 구간 시작 `_prerollFrames` 프레임 앞에 **꺼 둔 채** 스폰해 첫 포즈 · 머티리얼 · PSO 를 준비하고, 구간 동안 켜고, 끝나면 지웁니다(언리얼 Spawnable). 스폰한 오브젝트가 그 바인딩이다. */
        Appearance  = 5, /**< 시작 프레임을 지날 때 대상 오브젝트의 외형 프리셋을 `_preset` 으로 바꿉니다. 엔진은 외형을 모르므로 이벤트(`registerSequenceEvent`)로 알리고, 듣는 쪽(게임 프레임워크의 `SequenceAppearanceComponent`)이 바꿉니다. */
        Scenario    = 6, /**< 시작 프레임을 지날 때 시나리오 `_scenario` 를 재생합니다(`ScenarioPlayback::play` — 연출 안에 게임 행동 · 입력을 끼운다, 언리얼 시퀀서 이벤트 트랙이 블루프린트를 부르는 자리). */
        Animation   = 7, /**< 구간 동안 대상 애니메이터의 시퀀서 슬롯에 클립 `_clip` 을 구간 시작부터의 시간으로 섞습니다(언리얼 Skeletal Animation 트랙). 세기는 `_amplitude`(0..1)입니다. */
        Audio       = 8, /**< 시작 프레임을 지날 때 대상(또는 바인딩이 없으면 화면 소리) 에미터에서 이벤트 `_sound` 를 내고, 구간이 끝나면 그 소리를 멈춥니다(언리얼 Audio 트랙). */
        Subtitle    = 9, /**< 시작 프레임을 지날 때 자막 `_text`(현지화 키 `Namespace.Key`)를 구간 길이만큼 올립니다. 화자는 바인딩의 표시 이름(`SequenceBinding::_name`, 현지화 키 또는 글)입니다 — 씬 오브젝트 이름이 아니다(`ISubtitleSink`). */
        Count            /**< 표식입니다. 종류가 아닙니다. */
    };

    /** @brief 항목의 칸 하나입니다(`SequenceItemKindInfo` 의 필수 · 허용 칸 표가 이 순번의 비트입니다). */
    enum class SequenceItemField : uint8
    {
        Binding = 0, ///< `_binding` — 대상 오브젝트(스폰: 만들 오브젝트의 바인딩)
        Prefab,      ///< `_prefab`
        Preset,      ///< `_preset`
        Scenario,    ///< `_scenario`
        Clip,        ///< `_clip`
        Sound,       ///< `_sound`
        Text,        ///< `_text`
        Count
    };

    /** @brief 칸 하나의 비트입니다. */
    constexpr uint16 makeSequenceFieldBit( SequenceItemField field )
    {
        return static_cast<uint16>( 1u << static_cast<uint32>( field ) );
    }

    /** @brief 항목 종류 하나가 타임라인에 무엇을 하는지입니다. 표의 한 줄입니다(`SequenceAsset::findItemKindInfo`). */
    struct SequenceItemKindInfo
    {
        SequenceItemKind           _kind;                                                                                                                                /**< 종류입니다. 표의 순번과 같아야 합니다. */
        uint32                     _defaultColor;                                                                                                                        /**< 에디터에서 새 항목에 칠하는 색(0xAABBGGRR)입니다. */
        uint16                     _requiredFields;                                                                                                                      /**< 비어 있으면 읽기 오류인 칸(`makeSequenceFieldBit`)입니다. */
        uint16                     _allowedFields;                                                                                                                       /**< 적을 수 있는 칸입니다(필수 칸 포함). 그 밖의 칸이 차 있으면 읽기 오류입니다. */
        const SequenceSpanHandler* _pSpanHandler;                                                                                                                        /**< 구간에 들어갈 때 · 머무는 동안 · 나갈 때의 동작(애니메이션 · 소리 · 자막)입니다. 없으면 nullptr 입니다. */
        bool                       _bDrivesTarget; /**< 구간 동안 대상 오브젝트의 활성 상태를 정하는지 나타냅니다. */                                                    ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                       _bFiresOnCross; /**< 시작 프레임을 지날 때 이벤트로 알리는지입니다. */                                                                ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                       _bCutsCamera; /**< 구간 동안 대상의 카메라를 게임 카메라로 고르는지입니다. */                                                         ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                       _bShakesCamera; /**< 시작 프레임을 지날 때 대상 카메라에 흔들림 신호를 내는지입니다. */                                               ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                       _bSpawns; /**< 프리팹을 스폰해 구간 동안 켜는지입니다(`SequenceTimelineUtil::applySpawns`). 스폰한 오브젝트가 바인딩의 대상입니다. */ ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                       _bPlaysScenario; /**< 시작 프레임을 지날 때 `_scenario` 를 재생하는지입니다(`SequencePlayerComponent`). */                            ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief @p field 가 필수인지입니다. */
        bool requiresField( SequenceItemField field ) const { return ( _requiredFields & makeSequenceFieldBit( field ) ) != 0; }
        /** @brief @p field 를 적을 수 있는지입니다. */
        bool allowsField( SequenceItemField field ) const { return ( _allowedFields & makeSequenceFieldBit( field ) ) != 0; }
    };

    /**
     * @brief 키 트랙의 종류입니다. 파일에는 열거자 이름(`"Transform"`)으로 적힙니다.
     * @details 종류마다 채널 수가 정해져 있습니다(`SequenceKeyTrack::getChannelCount`).
     */
    ENUM()
    enum class SequenceTrackKind : int32
    {
        Transform  = 0, /**< 대상의 로컬 이동, 회전(라디안), 크기 아홉 채널입니다(언리얼의 3D Transform 트랙). */
        Property   = 1, /**< 대상 컴포넌트의 숫자 프로퍼티 하나입니다(내장 타입 표의 Number 갈래 — 정수 · 실수 · 성분이 하나인 압축 타입). */
        RigSlot    = 2, /**< 대상 `PoseModifierComponent` 의 리그 슬롯 가중치 하나입니다(`_propertyPath` 가 슬롯 이름, 값은 0..1 로 자른다). */
        CameraLens = 3, /**< 대상 `CameraComponent` 의 렌즈 다섯 채널입니다 — 세로 화각(라디안), 초점 거리(m), 조리개 f 값, 렌즈 시프트 x · y(언리얼 CineCamera 의 Current Focal Length · Focus · Aperture 키). */
        Count           /**< 개수를 세는 용도입니다. 종류가 아닙니다. */
    };

    /**
     * @brief 키 트랙 종류 하나의 특성입니다. 표의 한 줄입니다(`SequenceAsset::findTrackKindInfo`, 표는 `SequenceAsset.cpp` 의 `kArrSequenceTrackKindInfo`).
     * @details 새 종류는 값 하나와 이 표의 한 줄이다(static_assert 가 알려 준다) — 채널 수 · 새 이름 · 대상 경로 규칙을 에디터와 파일 검사가 같은 줄에서 읽는다.
     *          값을 읽고 쓰는 일(`SequenceTimelineUtil::readTrackValues` · `applyTrackValues`)은 종류마다 다른 코드라 그 switch 가 `Count` 를 빠뜨리면 컴파일러가 알린다.
     */
    struct SequenceTrackKindInfo
    {
        SequenceTrackKind _kind;                                                                                                                                   /**< 종류입니다. 표의 순번과 같아야 합니다. */
        uint32            _channelCount;                                                                                                                           /**< 채널 수입니다(`SequenceKeyTrack::getChannelCount`). */
        const utf8*       _pNamePrefix;                                                                                                                            /**< 새 트랙 이름의 머리입니다(에디터가 `Transform 1` 처럼 번호를 붙인다). */
        bool              _bUsesPropertyPath; /**< `_propertyPath` 로 대상 값을 고르는지입니다(프로퍼티 · 리그 슬롯) — 비어 있으면 재생이 아무것도 몰지 않는다. */ ///< NOLINT(sw-bool-flag): 집합체다(중괄호 초기화 표)
    };

    /** @brief 트랜스폼 트랙의 채널 수입니다(이동 xyz, 회전 xyz, 크기 xyz). */
    constexpr uint32 kSequenceTransformChannelCount = 9;
    /** @brief 렌즈 트랙의 채널 수입니다(화각, 초점 거리, 조리개, 시프트 x, 시프트 y). */
    constexpr uint32 kSequenceLensChannelCount = 5;
    /** @brief 키 트랙 한 줄이 가질 수 있는 가장 많은 채널 수입니다(값 버퍼의 크기). */
    constexpr uint32 kSequenceMaxChannelCount = kSequenceTransformChannelCount;
    /** @brief 키를 "그 프레임에 있다" 고 보는 시각 차입니다. 키는 정수 프레임에 찍히므로 반 프레임이면 이웃 키와 겹치지 않습니다. */
    constexpr float32 kSequenceKeyFrameTolerance = 0.5f;

    /**
     * @brief 재생이 끝나거나 멈출 때 시퀀스가 몰던 대상의 상태를 어떻게 남길지입니다(언리얼 섹션의 When Finished: Keep State · Restore State).
     * @details 정상 완료 · `stop()` · 씬을 나감(`onEndPlay`) · 트리거로 다시 틀기(먼저 멈춘다)가 모두 이 값을 따른다. 시퀀스가 만든 것(스폰 · 카메라 컷 · 애니메이션 덮어쓰기 · 소리)은
     *          값과 상관없이 늘 거둔다 — 시퀀스 밖에 남을 주인이 없다.
     */
    ENUM()
    enum class SequenceFinishPolicy : int32
    {
        RestoreState = 0, /**< 재생을 시작하기 전의 활성 상태 · 트랜스폼 · 프로퍼티 · 리그 가중치 · 렌즈로 되돌립니다(언리얼 기본값과 같다). */
        KeepState    = 1, /**< 마지막 프레임의 상태를 그대로 둡니다(문이 열린 채 남는 연출). */
    };
} // namespace sw

namespace sw
{
    /** @brief 트랜스폼 트랙 채널의 이름입니다(에디터 표시). 순서는 채널 순서입니다. */
    inline constexpr const utf8* kArrSequenceTransformChannelName[] = {
        "Location X",
        "Location Y",
        "Location Z",
        "Rotation X",
        "Rotation Y",
        "Rotation Z",
        "Scale X",
        "Scale Y",
        "Scale Z",
    };
    /** @brief 렌즈 트랙 채널의 이름입니다(에디터 표시). 순서는 채널 순서입니다. */
    inline constexpr const utf8* kArrSequenceLensChannelName[] = {
        "Field Of View",
        "Focus Distance",
        "Aperture",
        "Lens Shift X",
        "Lens Shift Y",
    };

    static_assert( SW_COUNT_OF( kArrSequenceTransformChannelName ) == kSequenceTransformChannelCount, "트랜스폼 채널마다 이름이 하나" );
    static_assert( SW_COUNT_OF( kArrSequenceLensChannelName ) == kSequenceLensChannelCount, "렌즈 채널마다 이름이 하나" );

    /** @brief 항목의 칸이 종류의 규칙과 어긋난 모양입니다(`SequenceTrackItem::findFieldIssue`). 어느 칸인지는 함께 돌려준다. */
    enum class SequenceItemFieldIssue : uint8
    {
        None = 0,        /**< 어긋나지 않았습니다. */
        UnknownKind,     /**< 종류 표에 없는 종류입니다. */
        MissingField,    /**< 종류가 요구하는 칸이 비었습니다. */
        FieldNotAllowed, /**< 종류가 쓰지 않는 칸이 차 있습니다. */
    };

    /**
     * @brief 시퀀스 안의 대상 자리 하나입니다. 항목과 키 트랙이 `_id` 로 가리키고, 재생하는 쪽이 이 id 에 오브젝트를 묶습니다.
     * @details id 는 시퀀스가 준 뒤 다시 쓰지 않는다(`SequenceAsset::_nextBindingID`) — 지운 바인딩의 id 가 새 바인딩에 가면 씬에 남은 묶음이 엉뚱한 대상을 몬다.
     */
    REFLECT()
    struct SW_API SequenceBinding
    {
        REFLECT_BODY();

        PROPERTY( Min = 1, Tooltip = "Stable id the items and key tracks refer to (never reused in this sequence)" )
        uint32 _id{ kSequenceNoBinding };
        PROPERTY( Meta = "Localizable", Tooltip = "Display name (localization key or text; the subtitle speaker); also the name a spawned object gets (not used to find scene objects)" )
        string _name;
    };

    /** @brief 시퀀서 트랙 항목입니다(클립, 이벤트, 카메라 컷, 카메라 흔들림, 스폰, 외형, 시나리오, 애니메이션, 소리, 자막). 구간 하나를 나타냅니다. */
    REFLECT()
    struct SW_API SequenceTrackItem
    {
        REFLECT_BODY();

        /** @brief `_prerollFrames` 가 이 값이면 시퀀스의 프리롤(`SequenceAsset::_cameraPrerollFrames`)을 씁니다. */
        static constexpr int32 kUseSequencePreroll = -1;

        PROPERTY( Tooltip = "Item name (event name for Event items)" )
        string _name;
        /** @brief 스폰: 구간 시작 몇 프레임 전(`_prerollFrames`)부터 꺼 둔 채 만들어 구간 동안 켤 프리팹입니다. 다른 종류는 비운다. */
        PROPERTY( AssetPath, AssetType = "Prefab", SkipIfEmpty, Tooltip = "Spawn: prefab spawned inactive before the span and shown during it" )
        string _prefab;
        /** @brief 카메라 컷: 대상의 카메라 디렉터가 켤 프리셋 id 입니다. 비우면 카메라를 그대로 씁니다. 외형: 대상이 입을 외형 프리셋 id 입니다(필수). */
        PROPERTY( SkipIfEmpty, Tooltip = "Camera cut: preset id the target's camera director turns on (empty: camera as is). Appearance: preset id the target wears (required)" )
        string _preset;
        /** @brief 시나리오: 시작 프레임을 지날 때 재생할 시나리오(`.scenario.xml` 리소스 id)입니다. 다른 종류는 비운다. */
        PROPERTY( SkipIfEmpty, Tooltip = "Scenario: scenario (.scenario.xml) played when the start frame is crossed" )
        string _scenario;
        /** @brief 애니메이션: 구간 동안 섞을 클립입니다 — 대상 애니메이션 셋의 클립 이름, 또는 클립 파일 경로(`SkeletalAnimatorComponent::makeClipPath`). 다른 종류는 비운다. */
        PROPERTY( AssetPath, AssetType = "AnimationClip", SkipIfEmpty, Tooltip = "Animation: clip blended over the target's animation during the span (a clip name of the target's animation set, or a clip file)" )
        string _clip;
        /** @brief 소리: 시작 프레임을 지날 때 낼 오디오 이벤트 이름입니다. 다른 종류는 비운다. */
        PROPERTY( SkipIfEmpty, Tooltip = "Audio: audio event posted when the start frame is crossed and stopped when the span ends" )
        string _sound;
        /** @brief 자막: 올릴 줄의 현지화 키(`Namespace.Key`)입니다. 다른 종류는 비운다. */
        PROPERTY( SkipIfEmpty, Meta = "Localizable", Tooltip = "Subtitle: localization key (Namespace.Key) or text of the line shown for the span" )
        string _text;
        /** @brief 대상 바인딩(`SequenceBinding::_id`)입니다. 0 은 없음이며, 종류가 대상을 요구하면 읽기 오류입니다. */
        PROPERTY( Tooltip = "Binding id of the object the item drives (0: none; Spawn: the binding the spawned object fills)" )
        uint32 _binding{ kSequenceNoBinding };
        PROPERTY( Tooltip = "First frame of the span" )
        int32 _start{ 0 };
        PROPERTY( Tooltip = "Last frame of the span" )
        int32 _end{ 10 };
        /** @brief 카메라 컷: 시작 몇 프레임 전에 대기 뷰 신호를 낼지입니다. 스폰: 시작 몇 프레임 전에 꺼 둔 채 만들지입니다. `kUseSequencePreroll` 이면 시퀀스의 값입니다. */
        PROPERTY( Tooltip = "Camera cut: frames before the start to signal the standby view; Spawn: frames before the start to spawn it inactive (-1: the sequence value)" )
        int32 _prerollFrames{ kUseSequencePreroll };
        /** @brief 카메라 흔들림: 최대 흔들림(m) 입니다. 애니메이션: 섞는 세기(0..1)입니다. */
        PROPERTY( Tooltip = "Camera shake: largest shake offset (m); Animation: blend weight (0..1)" )
        float32 _amplitude{ 1.0f };
        PROPERTY( Tooltip = "Editor colour (0xAABBGGRR)" )
        uint32 _color{ 0xFFAA8080 };
        PROPERTY( Tooltip = "Item kind" )
        SequenceItemKind _kind{ SequenceItemKind::Clip };

        /**
         * @brief 칸이 종류의 규칙과 어긋난 첫 모양입니다. 어긋난 칸을 @p pOutField 에 적습니다(nullptr 이면 적지 않는다).
         * @details 규칙은 종류 표 한 곳이다 — 파일 읽기(`SequenceAsset::validateFile`)와 에디터의 필수 칸 검사가 같은 답을 본다. 바인딩 id 가 시퀀스에 있는지는 시퀀스가 본다.
         */
        [[nodiscard]] SequenceItemFieldIssue findFieldIssue( SequenceItemField* pOutField = nullptr ) const;
        /** @brief 칸 @p field 가 차 있는지입니다(바인딩은 0 이 아니면 찼다). */
        [[nodiscard]] bool hasField( SequenceItemField field ) const;
        /** @brief 글 칸 @p field 입니다. 바인딩 · 범위 밖이면 nullptr 입니다(에디터가 칸을 표에서 그린다). */
        [[nodiscard]] string* accessTextField( SequenceItemField field );
        /** @brief 글 칸 @p field 입니다(읽기). 바인딩 · 범위 밖이면 nullptr 입니다. */
        [[nodiscard]] const string* findTextField( SequenceItemField field ) const;
        /** @brief 칸의 PROPERTY 이름(`_prefab`)입니다. 오류 · 에디터 표식에 씁니다. */
        static const utf8* getFieldPropertyName( SequenceItemField field );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SequenceKeyTrack
     * @brief 대상 하나의 값을 시각(프레임)마다 찍은 키로 몰아 가는 트랙입니다. 채널마다 `FloatCurve` 하나이고, 커브의 시각 단위는 프레임입니다.
     * @details 키가 없는 채널은 대상 값을 건드리지 않는다. 키를 찍으면(`setKey`) 모든 채널에 같은 시각의 키가 생긴다. 언리얼 Sequencer 의
     *          트랜스폼 키와 같다. 채널 하나만 고치면(`setChannelKey`) 그 채널에만 키가 생긴다.
     */
    REFLECT()
    struct SW_API SequenceKeyTrack
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Track name" )
        string _name;
        /** @brief 프로퍼티 트랙의 `<컴포넌트 타입>.<프로퍼티>` 입니다(트랜스폼 · 렌즈 트랙은 비어 있다). 리그 슬롯 트랙은 슬롯 이름입니다. */
        PROPERTY( Tooltip = "Property track: <component type>.<property>; rig slot track: slot name; transform and lens tracks: empty" )
        string _propertyPath;
        /** @brief 채널마다 커브 하나입니다. 수는 종류가 정한다(`getChannelCount`) — 다르면 읽기 오류입니다. */
        PROPERTY( Tooltip = "One curve per channel; the count is set by the kind" )
        vector<FloatCurve> _listChannel;
        /** @brief 대상 바인딩(`SequenceBinding::_id`)입니다. 0 이면 읽기 오류입니다. */
        PROPERTY( Min = 1, Tooltip = "Binding id of the object the track drives" )
        uint32 _binding{ kSequenceNoBinding };
        PROPERTY( Tooltip = "Track kind" )
        SequenceTrackKind _kind{ SequenceTrackKind::Transform };

        /** @brief 종류의 채널 수입니다. 모르는 종류면 0 입니다. */
        static uint32 getChannelCount( SequenceTrackKind kind );
        /** @brief 채널 이름입니다(트랜스폼 · 렌즈는 채널 이름, 프로퍼티는 프로퍼티 경로). 범위 밖이면 "?" 입니다. */
        const utf8* getChannelName( uint32 channelIndex ) const;
        /** @brief 채널 수를 종류에 맞춥니다(모르는 종류는 그대로). 새로 생긴 채널은 키가 없습니다. */
        void fitChannelsToKind();
        /** @brief @p frame 에 모든 채널의 키를 찍습니다(@p pArrValue 는 채널 수만큼). 그 프레임에 키가 있으면 값만 바꿉니다. */
        void setKey( float32 frame, const float32* pArrValue, uint32 valueCount );
        /** @brief 채널 하나의 @p frame 키를 찍습니다(있으면 값만 바꿉니다). 범위 밖 채널이면 false 입니다. */
        bool setChannelKey( uint32 channelIndex, float32 frame, float32 value );
        /** @brief 모든 채널에서 @p frame 의 키를 지웁니다. 지운 키가 있으면 true 입니다. */
        [[nodiscard]] bool removeKeysAt( float32 frame );
        /**
         * @brief 모든 채널에서 @p fromFrame 의 키를 @p toFrame 으로 옮깁니다(값 · 보간은 그대로). 옮긴 키가 있으면 true 입니다.
         * @details @p toFrame 에 이미 키가 있는 채널은 옮긴 키가 그 키를 덮는다(언리얼 Sequencer 에서 키를 다른 키 위에 놓은 것과 같다).
         */
        [[nodiscard]] bool moveKeysAt( float32 fromFrame, float32 toFrame );
        /** @brief 어느 채널이든 @p frame 에 키가 있으면 true 입니다. */
        bool hasKeyAt( float32 frame ) const;
        /** @brief 키가 있는 프레임을 모든 채널에서 모아 시각 순으로 채웁니다(겹치는 시각은 하나). */
        void collectKeyFrames( vector<float32>& outListFrame ) const;
        /** @brief 채널의 @p frame 값입니다. 채널이 없거나 키가 없으면 비어 있습니다 — 부르는 쪽이 "키 없음" 의 뜻(지금 값 유지 등)을 정한다. */
        [[nodiscard]] std::optional<float32> evaluateChannel( uint32 channelIndex, float32 frame ) const;
        /** @brief 채널 @p channelIndex 의 @p frame 키 인덱스를 반환합니다. 없으면 `constant::invalid_index::kUint32` 입니다. */
        uint32 findChannelKey( uint32 channelIndex, float32 frame ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceAsset
     * @brief 바인딩 · 구간 항목(클립, 이벤트, 카메라 컷 ...) · 키 트랙(트랜스폼, 프로퍼티, 리그 슬롯, 렌즈)을 담는 타임라인 에셋입니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigXMLDocument` — 모르는 속성 · 자식 · 모르는 종류 이름 · 읽지 못한 값 · 범위 밖 숫자는 오류).
     *          키는 PROPERTY 이름이 기준이다. 읽은 뒤 프레임 번호의 범위(`kSequenceFrameLimit`) · 순서, 종류마다의 칸 · 채널 수, 바인딩 id(유일 · 있음)를 따로 보고,
     *          커브는 시각 순으로 정렬해 자동 접선을 다시 센다.
     */
    REFLECT()
    class SW_API SequenceAsset
    {
    public:
        REFLECT_BODY();

        /** @brief 시퀀스 파일의 접미사입니다. 에디터 종류 등록(`Editor/AssetActions`)이 이 값을 씁니다. */
        static constexpr string_view kExtension = ".seq.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SequenceAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SequenceAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 빈 시퀀스를 만듭니다. */
        SequenceAsset() = default;

        /** @brief 리소스 id 의 XML 을 읽습니다. OS 경로는 받지 않습니다(`loadFromDiskFile`). 실패하면 빈 에셋이 남습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief **디스크 파일**의 XML 을 읽습니다. 리소스 id 로 풀지 않습니다(에디터 문서 · 도구 · 테스트용). 실패하면 빈 에셋이 남습니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief XML 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /**
         * @brief XML 본문을 파싱합니다.
         * @details **실패하면 빈 에셋이 남는다.** 일부만 비우면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아, 트랙 없는 이전
         *          시퀀스가 새 시퀀스인 척한다.
         */
        [[nodiscard]] bool loadFromXMLText( string_view xml );
        /** @brief XML 본문을 만듭니다(모든 PROPERTY). */
        string toXML() const;
        /** @brief 종류의 특성 항목을 반환합니다. 표에 없는 값이면 nullptr 입니다. */
        static const SequenceItemKindInfo* findItemKindInfo( SequenceItemKind kind );
        /** @brief 종류 표의 줄 수입니다(`SequenceItemKind::Count` 와 같다). 표를 도는 쪽(에디터 메뉴 · 테스트)이 `findItemKindInfo` 와 함께 쓴다. */
        static constexpr uint32 getItemKindCount() { return static_cast<uint32>( SequenceItemKind::Count ); }
        /** @brief 키 트랙 종류의 특성 줄입니다. 표에 없는 값이면 nullptr 입니다. 표를 도는 쪽(에디터의 트랙 더하기 단추)은 0 부터 `SequenceTrackKind::Count` 까지 묻는다. */
        static const SequenceTrackKindInfo* findTrackKindInfo( SequenceTrackKind kind );
        /** @brief 그 프레임에 걸쳐 있는 트랙 항목을 채웁니다. */
        void collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outListItem ) const;

        /** @brief 바인딩 @p bindingID 입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SequenceBinding* findBinding( uint32 bindingID ) const;
        /** @brief 이름 @p name 의 바인딩을 더하고 새 id 를 돌려줍니다. id 는 `_nextBindingID` 에서 받고 다시 쓰지 않습니다. */
        uint32 addBinding( string_view name );
        /** @brief 바인딩 @p bindingID 를 쓰는 항목 · 트랙 수입니다(에디터의 "쓰이지 않음" 표시 · 지우기 확인). */
        [[nodiscard]] uint32 countBindingUses( uint32 bindingID ) const;
        /** @brief 바인딩 @p bindingID 를 스폰하는 항목입니다. 스폰 바인딩이 아니면 nullptr 입니다 — 그 바인딩은 씬이 아니라 시퀀스가 채운다. */
        [[nodiscard]] const SequenceTrackItem* findSpawnItem( uint32 bindingID ) const;

    private:
        /** @brief 읽은 본문(@p xml)을 이 에셋에 채우고 의미를 봅니다. 파일 경로와 문자열 경로가 모이는 지점이며, 틀리면 빈 에셋이 남습니다. */
        [[nodiscard]] bool readText( string_view xml, string_view sourceLabel, const ResourceContext& context );
        /** @brief 프레임 번호의 범위 · 순서와 종류 · 채널 수를 봅니다. 칸 · 바인딩도 본다. 어긋난 곳마다 오류를 남깁니다. */
        [[nodiscard]] bool validateFile( string_view sourceLabel ) const;
        /** @brief 바인딩 표(유일 · 0 아님 · `_nextBindingID` 아래)를 봅니다. */
        [[nodiscard]] bool validateBindings( string_view sourceLabel ) const;

    public:
        /** @brief 카메라 컷 항목이 하나라도 있으면 true 를 반환합니다. 없으면 시퀀스가 게임 카메라를 건드리지 않습니다. */
        bool hasCameraCut() const;
        /** @brief @p frame 을 덮는 카메라 컷 가운데 가장 늦게 시작한 것입니다(같으면 뒤에 적힌 것). 없으면 nullptr 입니다. */
        const SequenceTrackItem* findCameraCut( int32 frame ) const;
        /** @brief 카메라 컷(대기 뷰) · 스폰 항목의 프리롤 프레임입니다. 항목이 정하지 않았으면 시퀀스의 값입니다. */
        int32 getPrerollFrames( const SequenceTrackItem& item ) const;

        PROPERTY( Tooltip = "First frame of the sequence" )
        int32 _frameMin{ 0 };
        PROPERTY( Tooltip = "Last frame of the sequence (after the first)" )
        int32 _frameMax{ 100 };
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Free note" )
        string _note;
        /** @brief 대상 자리입니다. 씬은 `SequencePlayerComponent::_listBinding` 으로 이 id 마다 오브젝트를 묶는다. */
        PROPERTY( Tooltip = "Objects the sequence drives, by stable id; the scene binds each id to an object" )
        vector<SequenceBinding> _listBinding;
        PROPERTY( Tooltip = "Span items (clip, event, camera cut, camera shake, spawn, appearance, scenario, animation, audio, subtitle)" )
        vector<SequenceTrackItem> _listItem;
        /** @brief 키 트랙(트랜스폼, 프로퍼티, 리그 슬롯, 렌즈)입니다. 클립이나 이벤트와 달리 구간이 아니라 키로 값을 몹니다. */
        PROPERTY( Tooltip = "Key tracks (transform, property, rig slot, camera lens)" )
        vector<SequenceKeyTrack> _listTrack;
        /** @brief 다음 바인딩이 받을 id 입니다. 지운 바인딩의 id 를 다시 주지 않는다(`SequenceBinding`). */
        PROPERTY( Min = 1, Tooltip = "Id the next binding gets (ids are never reused)" )
        uint32 _nextBindingID{ 1 };
        /** @brief 재생이 끝나거나 멈출 때 대상의 상태를 되돌릴지 둘지입니다(`SequenceFinishPolicy`). 파일마다 적는다(필수 — 계약을 고르지 않은 시퀀스를 두지 않는다). */
        PROPERTY( Required, Tooltip = "When playback finishes or stops: restore the state the sequence changed, or keep the last frame" )
        SequenceFinishPolicy _whenFinished{ SequenceFinishPolicy::RestoreState };
        /** @brief 게임 카메라에서 첫 카메라 컷으로 섞어 들어가는 시간(초)입니다. 0 이면 끊어 바꿉니다(언리얼 시퀀스의 Blend In). */
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Blend from the game camera into the first camera cut (0: cut)" )
        float32 _cameraBlendInSeconds{ 0.0f };
        /** @brief 마지막 카메라 컷에서 게임 카메라로 섞어 나오는 시간(초)입니다. 0 이면 끊어 바꿉니다(언리얼 시퀀스의 Blend Out). */
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Blend from the last camera cut back to the game camera (0: cut)" )
        float32 _cameraBlendOutSeconds{ 0.0f };
        /** @brief 카메라 컷 시작 몇 프레임 전에 대기 뷰 신호를 낼지의 기본값입니다(언리얼 Sequencer 의 preroll frames). 0 이면 내지 않습니다. */
        PROPERTY( Min = 0, Tooltip = "Default frames before a camera cut to signal the standby view (0: none)" )
        int32 _cameraPrerollFrames{ 0 };
    };
} // namespace sw
