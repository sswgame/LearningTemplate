/**
 * @file SpriteClipAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 스프라이트 클립(`.sprite.json`) 에셋입니다 — 아틀라스 · 프레임(UV 사각형 + 시간) · 이름 붙은 애니메이션.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimPlayback.h"

namespace sw
{
    class JSONValue;

    /**
     * @struct SpriteClipFrame
     * @brief 클립의 프레임 하나 — 아틀라스 안의 UV 사각형과 보여 줄 시간입니다.
     * @details UV 는 텍스처 크기에 대한 비율(0..1)이고 머티리얼 uvRect 와 같은 꼴 (u, v, 폭, 높이) 입니다. 폭이 음수면 좌우가 뒤집힌 프레임입니다.
     */
    struct SpriteClipFrame
    {
        float4 _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        /**
         * @brief 9-슬라이스 테두리 (왼쪽, 아래, 오른쪽, 위) — 이 프레임에 대한 비율(0..1)입니다. 모두 0 이면 테두리가 없습니다.
         * @details JSON 키 "border" 는 숫자 넷의 배열이고, 테두리가 없는 프레임은 쓰지 않습니다(그 키가 없는 파일과 바이트까지 같습니다).
         *          스프라이트의 자연 크기가 1 × 1 이라 비율이 곧 모서리의 월드 크기입니다(`SlicedSpriteDesc`). 유니티 Sprite Border · Godot patch margin 의 자리입니다.
         */
        float4 _border{ 0.0f, 0.0f, 0.0f, 0.0f };
        /** @brief 이 프레임을 보여 줄 시간(ms)입니다. 0 이하면 애니메이터의 프레임 속도를 씁니다(`getFrameDurationSeconds`). */
        int32 _durationMs{ 100 };

        /** @brief 테두리가 하나라도 있으면 true 입니다. */
        bool hasBorder() const { return _border._x > 0.0f || _border._y > 0.0f || _border._z > 0.0f || _border._w > 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SpriteClipKey
     * @brief 클립의 트랜스폼 키입니다(위치 · 회전을 시간에 따라 움직이는 선택 트랙). JSON 키는 "time" · "x" · "y" · "angleDeg" 입니다.
     * @details `_time` 은 **클립 타임라인**의 초입니다 — 프레임 0 이 0 초에 시작하고 프레임마다 그 프레임의 시간만큼 흐릅니다
     *          (`computeFrameStartSeconds`). 이름 붙은 애니메이션은 그 구간의 시각에서 트랙을 읽습니다. 값은 애니메이터가 움직이는
     *          스프라이트의 **로컬** 위치(x · y) · Z 축 회전(도)입니다(`SpriteAnimatorComponent`).
     */
    struct SpriteClipKey
    {
        float32 _time{ 0.0f };
        float2  _position{ 0.0f, 0.0f };
        float32 _angleDeg{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SpriteClipAnimation
     * @brief 이름 붙은 프레임 구간 하나입니다(예: "run" = 프레임 4..7). 애니메이터의 `play( name )` 이 이 이름으로 구간을 찾습니다.
     */
    struct SpriteClipAnimation
    {
        string                  _name;
        vector<AnimNotifyEvent> _listNotify; ///< 구간 시작 기준 시각(초)의 알림 — 스켈레탈 클립의 알림과 같은 의미(길이 > 0 이면 구간 알림)
        int32                   _firstFrame{ 0 };
        int32                   _frameCount{ 0 };
        uint8                   _bLoop{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SpriteClipAsset
     * @brief `.sprite.json` 하나입니다. 에디터(SpriteClipPanel)가 쓰고 런타임(SpriteComponent · SpriteAnimatorComponent)이 읽는 **한 벌의 파서**입니다.
     * @details 형식(키 이름)은 에디터가 처음부터 쓰던 그대로입니다:
     *          `{ "atlas": 경로, "frames": [ { "u", "v", "w", "h", "durationMs" } ], "transformKeys": [ { "time", "x", "y", "angleDeg" } ],
     *            "animations": [ { "name", "start", "count", "loop", "notifies": [ { "name", "time", "duration" } ] } ] }` — 알림 시각은 구간 시작 기준 초입니다.
     *          여기에 선택 배열 `"animations": [ { "name", "start", "count", "loop" } ]` 이 붙습니다. 이 배열이 없는 파일은 **프레임 전체가 이름
     *          없는 애니메이션 하나**입니다(`findFrameRange`).
     */
    class SW_API SpriteClipAsset
    {
    public:
        /** @brief 빈 클립을 만듭니다. */
        SpriteClipAsset() = default;

        /** @brief 파일(리소스 상대 · 절대 경로)을 읽습니다. 못 읽으면 false 이고 내용은 비어 있습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief JSON 본문을 읽습니다. 구문이 틀렸거나 루트가 객체가 아니면 false 이고 내용은 비어 있습니다. */
        [[nodiscard]] bool parseJSON( string_view json );
        /** @brief JSON 본문을 만듭니다(들여쓰기 2). 애니메이션이 없으면 "animations" 키를 쓰지 않습니다(그 키가 없는 파일과 바이트까지 같습니다). */
        string toJSON() const;
        /** @brief 내용을 비웁니다. */
        void clear();

        /** @brief 프레임 수입니다. */
        int32 getFrameCount() const { return static_cast<int32>( _listFrame.size() ); }
        /** @brief 프레임 하나입니다. 범위 밖이면 nullptr 입니다. */
        const SpriteClipFrame* findFrame( int32 frameIndex ) const;
        /** @brief 프레임을 보여 줄 시간(초)입니다. 프레임에 시간이 없거나(0 이하) 범위 밖이면 @p fallbackSeconds 입니다. */
        float32 getFrameDurationSeconds( int32 frameIndex, float32 fallbackSeconds ) const;
        /** @brief 이름으로 애니메이션을 찾습니다. 없으면 nullptr 입니다. */
        const SpriteClipAnimation* findAnimation( string_view name ) const;
        /**
         * @brief 이름의 프레임 구간(시작 · 개수 · 반복)을 찾습니다. 찾으면 true 이고 @p outRange 를 채웁니다(이름 칸은 건드리지 않습니다).
         * @details 이름 붙은 애니메이션이 하나도 없는 클립은 **어떤 이름이든 프레임 전체**를 반복 구간으로 답합니다 — 클립 하나가 곧
         *          애니메이션 하나입니다. 이름 붙은 것이 있는데 그 이름이 없으면 false 입니다.
         */
        bool findFrameRange( string_view name, SpriteClipAnimation& outRange ) const;
        /** @brief 클립 타임라인에서 프레임 @p frameIndex 가 시작하는 시각(초)입니다 — 앞 프레임들의 시간 합이고, 시간이 없는 프레임은 @p fallbackSeconds 로 셉니다. */
        float32 computeFrameStartSeconds( int32 frameIndex, float32 fallbackSeconds ) const;
        /** @brief 트랜스폼 키가 하나라도 있으면 true 입니다. */
        bool hasTransformKeys() const { return _listKey.empty() == false; }
        /**
         * @brief 트랜스폼 트랙을 클립 시각 @p clipSeconds 에서 샘플해 @p outKey 에 채웁니다. 키가 없으면 false 입니다.
         * @details 두 키 사이는 선형 보간이고, 첫 키 앞 · 마지막 키 뒤는 그 키의 값을 유지합니다(유니티 커브의 Clamp). 키는 시간 순서가
         *          아니어도 됩니다 — 에디터는 추가한 순서로 씁니다. 시각이 같은 키가 여럿이면 목록의 앞쪽 것입니다.
         */
        bool sampleTransformKey( float32 clipSeconds, SpriteClipKey& outKey ) const;

        string                      _atlasPath;     ///< 아틀라스 텍스처 경로("atlas")입니다
        vector<SpriteClipFrame>     _listFrame;     ///< 프레임("frames")입니다
        vector<SpriteClipKey>       _listKey;       ///< 트랜스폼 키("transformKeys")입니다
        vector<SpriteClipAnimation> _listAnimation; ///< 이름 붙은 구간("animations")입니다. 그 키가 없는 파일은 비어 있습니다

    private:
        /** @brief 파싱된 루트에서 내용을 읽습니다. 루트가 객체가 아니면 false 입니다. */
        [[nodiscard]] bool parseRoot( const JSONValue& root, string_view sourceLabel );
    };
} // namespace sw
