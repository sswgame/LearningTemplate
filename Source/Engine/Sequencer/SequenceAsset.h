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
    class JSONValue;

    /**
     * @brief 프레임 번호가 가질 수 있는 절댓값 상한입니다.
     * @details 프레임 번호는 JSON 에서 옵니다. 그대로 믿으면 안 됩니다. 타임라인 코드는 두 프레임의
     *          **차**를 그냥 빼기로 구하고(`_frameMax - _frameMin`, `_end - _start`), int32 의
     *          양 끝값이 짝으로 들어오면 그 빼기가 부호 있는 넘침(UB)이 됩니다. 범위를 절반으로
     *          자르면 어떤 두 값의 차도 반드시 int32 안에 들어오므로, 빼는 자리마다 넓은 타입으로
     *          올리지 않아도 됩니다. 30fps 기준 1,000만 일이 넘는 길이라 실사용을 자르지 않습니다.
     */
    constexpr int32 kSequenceFrameLimit = MathUtil::kMaxInt32 / 2;

    /**
     * @brief 시퀀서 트랙 항목의 종류입니다. JSON 에는 정수(`"type"`)로 적힙니다 — 값을 바꾸면 기존 시퀀스 파일이 다른 종류로 읽힙니다.
     * @details 종류를 더하면 값 하나와 `kArrSequenceItemKindInfo` 의 줄 하나를 더합니다(static_assert 가 짚습니다).
     */
    ENUM()
    enum class SequenceItemKind : int32
    {
        Clip      = 0, /**< 구간 동안 대상을 켭니다(유니티 Timeline 의 Activation 트랙). 움직임은 키 트랙(`SequenceKeyTrack`)이 정합니다. */
        Event     = 1, /**< 시작 프레임을 지날 때 한 번 알립니다(`SequencePlayerComponent::registerSequenceEvent`). */
        CameraCut = 2, /**< 구간 동안 대상의 카메라가 게임 카메라입니다(언리얼 Camera Cut 트랙, `CameraRegistry::setCutCamera`). */
        Count          /**< 표식입니다. 종류가 아닙니다. */
    };

    /** @brief 항목 종류 하나가 타임라인에 무엇을 하는지입니다. */
    struct SequenceItemKindInfo
    {
        SequenceItemKind _kind;          /**< 종류입니다. 표의 순번과 같아야 합니다. */
        uint32           _defaultColor;  /**< 에디터에서 새 항목에 칠하는 색(0xAABBGGRR)입니다. */
        bool             _bDrivesTarget; /**< 구간 동안 대상 오브젝트의 활성 · 트랜스폼을 정하는지입니다. */
        bool             _bFiresOnCross; /**< 시작 프레임을 지날 때 이벤트로 알리는지입니다. */
        bool             _bCutsCamera;   /**< 구간 동안 대상의 카메라를 게임 카메라로 고르는지입니다. */
    };

    /**
     * @brief 키 트랙의 종류입니다. JSON 에는 정수(`"type"`)로 적힙니다.
     * @details 종류마다 채널 수가 정해져 있습니다(`SequenceKeyTrack::getChannelCount`).
     */
    ENUM()
    enum class SequenceTrackKind : int32
    {
        Transform = 0, /**< 대상의 로컬 이동 · 회전(라디안) · 크기 아홉 채널입니다(언리얼 3D Transform 트랙). */
        Property  = 1, /**< 대상 컴포넌트의 숫자 프로퍼티 하나(`float32` · `int32`)입니다. */
        Count          /**< 표식입니다. 종류가 아닙니다. */
    };

    /** @brief 트랜스폼 트랙의 채널 수입니다(이동 xyz · 회전 xyz · 크기 xyz). */
    constexpr uint32 kSequenceTransformChannelCount = 9;
    /** @brief 키를 "그 프레임에 있다" 고 보는 시각 차입니다. 키는 정수 프레임에 찍히므로 반 프레임이면 이웃 키와 겹치지 않습니다. */
    constexpr float32 kSequenceKeyFrameTolerance = 0.5f;
} // namespace sw

namespace sw
{
    /** @brief 항목 종류 표입니다. **종류마다 한 줄이고 순서는 `SequenceItemKind` 값 순서입니다.** */
    inline constexpr SequenceItemKindInfo kArrSequenceItemKindInfo[] = {
        {     SequenceItemKind::Clip, 0xFF80AA80u,  true, false, false},
        {    SequenceItemKind::Event, 0xFF8080AAu, false,  true, false},
        {SequenceItemKind::CameraCut, 0xFFAA80AAu, false, false,  true},
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

    /** @brief 시퀀서 트랙 항목입니다(클립 · 이벤트 · 카메라 컷 — 구간 하나). */
    struct SequenceTrackItem
    {
        string           _name;
        string           _targetObject;
        int32            _start{ 0 };
        int32            _end{ 10 };
        uint32           _color{ 0xFFAA8080 };
        SequenceItemKind _kind{ SequenceItemKind::Clip }; /**< 표에 없는 값(새 버전 파일)은 읽고 다시 쓰지만 적용하지 않습니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SequenceKeyTrack
     * @brief 대상 하나의 값을 시각(프레임)마다 찍은 키로 모는 트랙입니다. 채널마다 `FloatCurve` 하나이고, 커브의 시각 단위는 프레임입니다.
     * @details 키가 없는 채널은 대상 값을 건드리지 않습니다. 키를 찍으면(`setKey`) 모든 채널에 같은 시각의 키가 생깁니다 — 언리얼 Sequencer 의
     *          트랜스폼 키와 같습니다. 채널 하나만 고치면(`setChannelKey`) 그 채널에만 키가 생깁니다.
     */
    struct SW_API SequenceKeyTrack
    {
        string             _name;
        string             _targetObject;
        string             _propertyPath; /**< 프로퍼티 트랙의 `<컴포넌트 타입>.<프로퍼티>` 입니다(트랜스폼 트랙은 비어 있다). */
        vector<FloatCurve> _listChannel;
        SequenceTrackKind  _kind{ SequenceTrackKind::Transform }; /**< 표에 없는 값은 읽고 다시 쓰지만 적용하지 않습니다. */

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
        /** @brief 채널의 @p frame 값입니다. 키가 없으면 @p fallback 입니다. */
        float32 evaluateChannel( uint32 channelIndex, float32 frame, float32 fallback ) const;
        /** @brief 채널 @p channelIndex 의 @p frame 키 자리입니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findChannelKey( uint32 channelIndex, float32 frame ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceAsset
     * @brief 구간 항목(클립 · 이벤트 · 카메라 컷)과 키 트랙(트랜스폼 · 프로퍼티)을 담는 타임라인 에셋입니다.
     */
    class SW_API SequenceAsset
    {
    public:
        /** @brief 빈 시퀀스를 만듭니다. */
        SequenceAsset() = default;

        /**
         * @brief JSON 파일을 읽습니다.
         * @details 읽은 문서를 그대로 읽습니다(다시 문자열로 덤프해 재파싱하지 않습니다).
         */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /**
         * @brief JSON 본문을 파싱합니다.
         * @details **실패하면 빈 에셋이 남습니다** — 일부만 비우면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아 트랙 없는 옛
         *          시퀀스가 새 시퀀스인 척합니다.
         */
        [[nodiscard]] bool parseJSON( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJSON() const;
        /** @brief 종류의 특성 줄입니다. 표에 없는 값이면 nullptr 입니다. */
        static const SequenceItemKindInfo* findItemKindInfo( SequenceItemKind kind );
        /** @brief 그 프레임에 걸쳐 있는 트랙 항목을 채웁니다. */
        void collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outListItem ) const;

    private:
        /** @brief 파싱된 루트 하나를 읽습니다. 파일 경로와 문자열 경로가 모이는 자리입니다. */
        [[nodiscard]] bool parseRoot( const JSONValue& root );

    public:
        /** @brief 카메라 컷 항목이 하나라도 있으면 true 입니다 — 없으면 시퀀스가 게임 카메라를 건드리지 않습니다. */
        bool hasCameraCut() const;

        int32                     _frameMin{ 0 };
        int32                     _frameMax{ 100 };
        string                    _note;
        vector<SequenceTrackItem> _listItem;
        vector<SequenceKeyTrack>  _listTrack; /**< 키 트랙(트랜스폼 · 프로퍼티)입니다. 클립 · 이벤트와 달리 구간이 아니라 키로 값을 몹니다. */
    };
} // namespace sw
