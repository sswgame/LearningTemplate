/**
 * @file SequenceAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 시퀀서 타임라인 JSON 에셋입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/FloatCurve.h"

namespace sw
{
    /**
     * @brief 프레임 번호가 가질 수 있는 절댓값 상한입니다.
     * @details 프레임 번호는 JSON 에서 오므로 그대로 믿으면 안 된다. 타임라인 코드는 두 프레임의
     *          **차**를 그냥 빼기로 구하는데(`_frameMax - _frameMin`, `_end - _start`), int32 의
     *          양 끝값이 쌍으로 들어오면 그 빼기가 부호 있는 오버플로(UB)가 된다. 범위를 절반으로
     *          묶으면 어떤 두 값의 차도 반드시 int32 안에 들어오므로, 빼는 곳마다 넓은 타입으로
     *          올리지 않아도 된다. 30fps 기준 1,000만 일이 넘는 길이라 실사용을 막지 않는다. 밖의 값은 읽기 오류다.
     */
    constexpr int32 kSequenceFrameLimit = MathUtil::kMaxInt32 / 2;

    /**
     * @brief 시퀀서 트랙 항목의 종류입니다. 파일에는 열거자 이름(`"CameraCut"`)으로 적힙니다. 이름을 바꾸면 데이터를 다시 씁니다.
     * @details 종류를 추가하면 값 하나와 `kArrSequenceItemKindInfo` 의 항목 하나를 추가한다(static_assert 가 알려 준다).
     */
    ENUM()
    enum class SequenceItemKind : int32
    {
        Clip        = 0, /**< 구간 동안 대상을 켭니다(유니티 Timeline 의 Activation 트랙). 움직임은 키 트랙(`SequenceKeyTrack`)이 정합니다. */
        Event       = 1, /**< 시작 프레임을 지날 때 한 번 알립니다(`SequencePlayerComponent::registerSequenceEvent`). */
        CameraCut   = 2, /**< 구간 동안 대상의 카메라가 게임 카메라입니다(언리얼 Camera Cut 트랙, `CameraRegistry::setCutCamera`). `_preset` 이 있으면 대상의 카메라 디렉터가 그 프리셋을 켭니다. */
        CameraShake = 3, /**< 구간 동안 대상 카메라를 흔듭니다(언리얼 Camera Shake 트랙). 세기는 `_amplitude`, 길이는 구간입니다. */
        Count            /**< 표식입니다. 종류가 아닙니다. */
    };

    /** @brief 항목 종류 하나가 타임라인에 무엇을 하는지입니다. */
    struct SequenceItemKindInfo
    {
        SequenceItemKind _kind;          /**< 종류입니다. 테이블의 순번과 같아야 합니다. */
        uint32           _defaultColor;  /**< 에디터에서 새 항목에 칠하는 색(0xAABBGGRR)입니다. */
        bool             _bDrivesTarget; /**< 구간 동안 대상 오브젝트의 활성 상태와 트랜스폼을 정하는지 나타냅니다. */
        bool             _bFiresOnCross; /**< 시작 프레임을 지날 때 이벤트로 알리는지입니다. */
        bool             _bCutsCamera;   /**< 구간 동안 대상의 카메라를 게임 카메라로 고르는지입니다. */
        bool             _bShakesCamera; /**< 시작 프레임을 지날 때 대상 카메라에 흔들림 신호를 내는지입니다. */
    };

    /**
     * @brief 키 트랙의 종류입니다. 파일에는 열거자 이름(`"Transform"`)으로 적힙니다.
     * @details 종류마다 채널 수가 정해져 있습니다(`SequenceKeyTrack::getChannelCount`).
     */
    ENUM()
    enum class SequenceTrackKind : int32
    {
        Transform = 0, /**< 대상의 로컬 이동, 회전(라디안), 크기 아홉 채널입니다(언리얼의 3D Transform 트랙). */
        Property  = 1, /**< 대상 컴포넌트의 숫자 프로퍼티 하나입니다(`float32` 또는 `int32`). */
        RigSlot   = 2, /**< 대상 `PoseModifierComponent` 의 리그 슬롯 가중치 하나입니다(`_propertyPath` 가 슬롯 이름, 값은 0..1 로 자른다). */
        Count          /**< 개수를 세는 용도입니다. 종류가 아닙니다. */
    };

    /** @brief 트랜스폼 트랙의 채널 수입니다(이동 xyz, 회전 xyz, 크기 xyz). */
    constexpr uint32 kSequenceTransformChannelCount = 9;
    /** @brief 키를 "그 프레임에 있다" 고 보는 시각 차입니다. 키는 정수 프레임에 찍히므로 반 프레임이면 이웃 키와 겹치지 않습니다. */
    constexpr float32 kSequenceKeyFrameTolerance = 0.5f;
} // namespace sw

namespace sw
{
    /** @brief 항목 종류 표입니다. **종류마다 한 줄이고 순서는 `SequenceItemKind` 값 순서입니다.** */
    inline constexpr SequenceItemKindInfo kArrSequenceItemKindInfo[] = {
        {       SequenceItemKind::Clip, 0xFF80AA80u,  true, false, false, false},
        {      SequenceItemKind::Event, 0xFF8080AAu, false,  true, false, false},
        {  SequenceItemKind::CameraCut, 0xFFAA80AAu, false, false,  true, false},
        {SequenceItemKind::CameraShake, 0xFF80A0C0u, false, false, false,  true},
    };

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

    static_assert( SW_COUNT_OF( kArrSequenceTransformChannelName ) == kSequenceTransformChannelCount, "트랜스폼 채널마다 이름이 하나" );

    static_assert( SW_COUNT_OF( kArrSequenceItemKindInfo ) == static_cast<size_t>( SequenceItemKind::Count ),
                   "SequenceItemKind 를 늘렸으면 kArrSequenceItemKindInfo 에도 줄을 더할 것" );

    /** @brief 시퀀서 트랙 항목입니다(클립, 이벤트, 카메라 컷, 카메라 흔들림). 구간 하나를 나타냅니다. */
    REFLECT()
    struct SW_API SequenceTrackItem
    {
        REFLECT_BODY();

        /** @brief `_prerollFrames` 가 이 값이면 시퀀스의 프리롤(`SequenceAsset::_cameraPrerollFrames`)을 씁니다. */
        static constexpr int32 kUseSequencePreroll = -1;

        PROPERTY( Tooltip = "Item name (event name for Event items)" )
        string _name;
        PROPERTY( Tooltip = "Name of the object the item drives" )
        string _targetObject;
        /** @brief 카메라 컷: 대상의 카메라 디렉터가 켤 프리셋 id 입니다. 비우면 카메라를 그대로 씁니다. */
        PROPERTY( Tooltip = "Camera cut: preset id the target's camera director turns on (empty: camera as is)" )
        string _preset;
        PROPERTY( Tooltip = "First frame of the span" )
        int32 _start{ 0 };
        PROPERTY( Tooltip = "Last frame of the span" )
        int32 _end{ 10 };
        /** @brief 카메라 컷: 시작 몇 프레임 전에 대기 뷰 신호를 낼지입니다. `kUseSequencePreroll` 이면 시퀀스의 값입니다. */
        PROPERTY( Tooltip = "Camera cut: frames before the start to signal the standby view (-1: the sequence value)" )
        int32 _prerollFrames{ kUseSequencePreroll };
        /** @brief 카메라 흔들림: 최대 흔들림(m) 입니다. */
        PROPERTY( Units = m, Tooltip = "Camera shake: largest shake offset" )
        float32 _amplitude{ 1.0f };
        PROPERTY( Tooltip = "Editor colour (0xAABBGGRR)" )
        uint32 _color{ 0xFFAA8080 };
        PROPERTY( Tooltip = "Item kind" )
        SequenceItemKind _kind{ SequenceItemKind::Clip };
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
        PROPERTY( Tooltip = "Name of the object the track drives" )
        string _targetObject;
        /** @brief 프로퍼티 트랙의 `<컴포넌트 타입>.<프로퍼티>` 입니다(트랜스폼 트랙은 비어 있다). 리그 슬롯 트랙은 슬롯 이름입니다. */
        PROPERTY( Tooltip = "Property track: <component type>.<property>; rig slot track: slot name; transform track: empty" )
        string _propertyPath;
        /** @brief 채널마다 커브 하나입니다. 수는 종류가 정한다(`getChannelCount`) — 다르면 읽기 오류입니다. */
        PROPERTY( Tooltip = "One curve per channel; the count is set by the kind" )
        vector<FloatCurve> _listChannel;
        PROPERTY( Tooltip = "Track kind" )
        SequenceTrackKind _kind{ SequenceTrackKind::Transform };

        /** @brief 종류의 채널 수입니다. 모르는 종류면 0 입니다. */
        static uint32 getChannelCount( SequenceTrackKind kind );
        /** @brief 채널 이름입니다(트랜스폼은 축 이름, 프로퍼티는 프로퍼티 경로). 범위 밖이면 "?" 입니다. */
        const utf8* getChannelName( uint32 channelIndex ) const;
        /** @brief 채널 수를 종류에 맞춥니다(모르는 종류는 그대로). 새로 생긴 채널은 키가 없습니다. */
        void fitChannelsToKind();
        /** @brief @p frame 에 모든 채널의 키를 찍습니다(@p pArrValue 는 채널 수만큼). 그 프레임에 키가 있으면 값만 바꿉니다. */
        void setKey( float32 frame, const float32* pArrValue, uint32 valueCount );
        /** @brief 채널 하나의 @p frame 키를 찍습니다(있으면 값만 바꿉니다). 범위 밖 채널이면 false 입니다. */
        bool setChannelKey( uint32 channelIndex, float32 frame, float32 value );
        /** @brief 모든 채널에서 @p frame 의 키를 지웁니다. 지운 키가 있으면 true 입니다. */
        [[nodiscard]] bool removeKeysAt( float32 frame );
        /** @brief 어느 채널이든 @p frame 에 키가 있으면 true 입니다. */
        bool hasKeyAt( float32 frame ) const;
        /** @brief 키가 있는 프레임을 모든 채널에서 모아 시각 순으로 채웁니다(겹치는 시각은 하나). */
        void collectKeyFrames( vector<float32>& outListFrame ) const;
        /** @brief 채널의 @p frame 값입니다. 채널이 없거나 키가 없으면 비어 있습니다 — 부르는 쪽이 "키 없음" 의 뜻(지금 값 유지 등)을 정한다. */
        [[nodiscard]] std::optional<float32> evaluateChannel( uint32 channelIndex, float32 frame ) const;
        /** @brief 채널 @p channelIndex 의 @p frame 키 인덱스를 반환합니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findChannelKey( uint32 channelIndex, float32 frame ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceAsset
     * @brief 구간 항목(클립, 이벤트, 카메라 컷)과 키 트랙(트랜스폼, 프로퍼티)을 담는 타임라인 에셋입니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigJSON` — 모르는 키 · 모르는 종류 이름 · 읽지 못한 값 · 범위 밖 숫자는 오류).
     *          키는 PROPERTY 이름이 정본이다. 읽은 뒤 프레임 번호의 범위(`kSequenceFrameLimit`) · 순서, 종류마다의 채널 수를 따로 보고,
     *          커브는 시각 순으로 정렬해 자동 접선을 다시 센다.
     */
    REFLECT()
    class SW_API SequenceAsset
    {
    public:
        REFLECT_BODY();

        /** @brief 시퀀스 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".seq.json";

        /** @brief 빈 시퀀스를 만듭니다. */
        SequenceAsset() = default;

        /** @brief JSON 파일(리소스 상대 경로나 절대 경로)을 읽습니다. 실패하면 빈 에셋이 남습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /**
         * @brief JSON 본문을 파싱합니다.
         * @details **실패하면 빈 에셋이 남는다.** 일부만 비우면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아, 트랙 없는 이전
         *          시퀀스가 새 시퀀스인 척한다.
         */
        [[nodiscard]] bool parseJSON( string_view json );
        /** @brief JSON 본문을 만듭니다(들여쓰기 2, 모든 PROPERTY). */
        string toJSON() const;
        /** @brief 종류의 특성 항목을 반환합니다. 테이블에 없는 값이면 nullptr 입니다. */
        static const SequenceItemKindInfo* findItemKindInfo( SequenceItemKind kind );
        /** @brief 그 프레임에 걸쳐 있는 트랙 항목을 채웁니다. */
        void collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outListItem ) const;

    private:
        /** @brief 읽은 본문(@p json)을 이 에셋에 채우고 의미를 봅니다. 파일 경로와 문자열 경로가 모이는 지점이며, 틀리면 빈 에셋이 남습니다. */
        [[nodiscard]] bool readText( string_view json, string_view sourceLabel );
        /** @brief 프레임 번호의 범위 · 순서와 종류 · 채널 수를 봅니다. 어긋난 곳마다 오류를 남깁니다. */
        [[nodiscard]] bool validateFile( string_view sourceLabel ) const;

    public:
        /** @brief 카메라 컷 항목이 하나라도 있으면 true 를 반환합니다. 없으면 시퀀스가 게임 카메라를 건드리지 않습니다. */
        bool hasCameraCut() const;
        /** @brief @p frame 을 덮는 카메라 컷 가운데 가장 늦게 시작한 것입니다(같으면 뒤에 적힌 것). 없으면 nullptr 입니다. */
        const SequenceTrackItem* findCameraCut( int32 frame ) const;
        /** @brief 카메라 컷 항목의 프리롤 프레임입니다. 항목이 정하지 않았으면 시퀀스의 값입니다. */
        int32 getPrerollFrames( const SequenceTrackItem& item ) const;

        PROPERTY( Tooltip = "First frame of the sequence" )
        int32 _frameMin{ 0 };
        PROPERTY( Tooltip = "Last frame of the sequence (after the first)" )
        int32 _frameMax{ 100 };
        PROPERTY( Tooltip = "Free note" )
        string _note;
        PROPERTY( Tooltip = "Span items (clip, event, camera cut, camera shake)" )
        vector<SequenceTrackItem> _listItem;
        /** @brief 키 트랙(트랜스폼, 프로퍼티)입니다. 클립이나 이벤트와 달리 구간이 아니라 키로 값을 몹니다. */
        PROPERTY( Tooltip = "Key tracks (transform, property, rig slot)" )
        vector<SequenceKeyTrack> _listTrack;
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
