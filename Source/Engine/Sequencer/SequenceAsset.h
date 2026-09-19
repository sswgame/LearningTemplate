/**
 * @file SequenceAsset.h
 * @brief 에디터/런타임이 공유하는 시퀀서 타임라인 JSON 애셋
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class JsonValue;

    /**
     * @brief 프레임 번호가 가질 수 있는 절댓값 상한입니다.
     * @details 프레임 번호는 JSON 에서 온다 — 그대로 믿으면 안 된다. 타임라인 코드는 두 프레임의
     *          **차**를 그냥 빼기로 구하고(`_frameMax - _frameMin`, `_end - _start`), int32 의
     *          양 끝값이 짝으로 들어오면 그 빼기가 부호 있는 넘침(UB)이 된다. 범위를 절반으로
     *          자르면 어떤 두 값의 차도 반드시 int32 안에 들어오므로, 빼는 자리마다 넓은 타입으로
     *          올리지 않아도 된다. 30fps 기준 1,000만 일이 넘는 길이라 실사용을 자르지 않는다.
     */
    constexpr int32 kSequenceFrameLimit = MathUtil::MaxInt32 / 2;

    /** @brief 시퀀서 트랙 항목 (클립 또는 이벤트) */
    struct SequenceTrackItem
    {
        string _name;
        string _targetObject;
        float3 _translation{};
        float3 _rotation{};
        float3 _scale{ 1.0f, 1.0f, 1.0f };
        int32  _start{ 0 };
        int32  _end{ 10 };
        int32  _type{ 0 };
        uint32 _color{ 0xFFAA8080 };
    };

    /**
     * @class SequenceAsset
     * @brief 카메라/오브젝트 트랙을 담는 타임라인 애셋
     */
    class SW_API SequenceAsset
    {
    public:
        /** @brief 빈 시퀀스를 만듭니다. */
        SequenceAsset() = default;

        /**
         * @brief JSON 파일을 읽습니다.
         * @details 예전에는 문서를 읽은 뒤 **다시 문자열로 덤프해 재파싱**했다 — 같은 파일을
         *          두 번 파싱하는 일이었다. 지금은 읽은 문서를 그대로 읽는다.
         */
        bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        bool saveToFile( string_view path ) const;
        /**
         * @brief JSON 본문을 파싱합니다.
         * @details **실패하면 빈 애셋이 남는다.** 예전에는 `_listItem` 만 비우고 실패해서
         *          앞 시퀀스의 프레임 범위와 노트가 그대로 남았다 — 트랙 없는 옛 시퀀스가
         *          새 시퀀스인 척했다.
         */
        bool parseJson( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJson() const;
        /** @brief 해당 프레임에 걸쳐 있는 트랙을 채웁니다. */
        void collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outListItem ) const;

    private:
        /** @brief 파싱된 루트 하나를 읽습니다 — 파일 경로와 문자열 경로가 모이는 자리. */
        bool parseRoot( const JsonValue& root );

    public:
        int32                     _frameMin{ 0 };
        int32                     _frameMax{ 100 };
        string                    _note;
        vector<SequenceTrackItem> _listItem;
    };
} // namespace sw
