/**
 * @file SequenceAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 시퀀서 타임라인 JSON 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

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
        Clip  = 0, /**< 구간 동안 대상을 켜고 트랜스폼을 보간합니다. */
        Event = 1, /**< 시작 프레임을 지날 때 한 번 알립니다(`SequencePlayerComponent::registerSequenceEvent`). */
        Count      /**< 표식입니다. 종류가 아닙니다. */
    };

    /** @brief 항목 종류 하나가 타임라인에 무엇을 하는지입니다. */
    struct SequenceItemKindInfo
    {
        SequenceItemKind _kind;          /**< 종류입니다. 표의 순번과 같아야 합니다. */
        uint32           _defaultColor;  /**< 에디터에서 새 항목에 칠하는 색(0xAABBGGRR)입니다. */
        bool             _bDrivesTarget; /**< 구간 동안 대상 오브젝트의 활성 · 트랜스폼을 정하는지입니다. */
        bool             _bFiresOnCross; /**< 시작 프레임을 지날 때 이벤트로 알리는지입니다. */
    };
} // namespace sw

namespace sw
{
    /** @brief 항목 종류 표입니다. **종류마다 한 줄이고 순서는 `SequenceItemKind` 값 순서입니다.** */
    inline constexpr SequenceItemKindInfo kArrSequenceItemKindInfo[] = {
        { SequenceItemKind::Clip, 0xFF80AA80u,  true, false},
        {SequenceItemKind::Event, 0xFF8080AAu, false,  true},
    };

    static_assert( SW_COUNT_OF( kArrSequenceItemKindInfo ) == static_cast<size_t>( SequenceItemKind::Count ),
                   "SequenceItemKind 를 늘렸으면 kArrSequenceItemKindInfo 에도 줄을 더할 것" );

    /** @brief 시퀀서 트랙 항목입니다(클립 또는 이벤트). */
    struct SequenceTrackItem
    {
        string           _name;
        string           _targetObject;
        float3           _translation{};
        float3           _rotation{};
        float3           _scale{ 1.0f, 1.0f, 1.0f };
        int32            _start{ 0 };
        int32            _end{ 10 };
        uint32           _color{ 0xFFAA8080 };
        SequenceItemKind _kind{ SequenceItemKind::Clip }; /**< 표에 없는 값(새 버전 파일)은 읽고 다시 쓰지만 적용하지 않습니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceAsset
     * @brief 카메라 · 오브젝트 트랙을 담는 타임라인 에셋입니다.
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
        int32                     _frameMin{ 0 };
        int32                     _frameMax{ 100 };
        string                    _note;
        vector<SequenceTrackItem> _listItem;
    };
} // namespace sw
