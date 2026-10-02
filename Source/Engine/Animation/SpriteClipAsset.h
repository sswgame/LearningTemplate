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

namespace sw
{
    class JsonValue;

    /**
     * @struct SpriteClipFrame
     * @brief 클립의 프레임 하나 — 아틀라스 안의 UV 사각형과 보여 줄 시간입니다.
     * @details UV 는 텍스처 크기에 대한 비율(0..1)이고 머티리얼 uvRect 와 같은 꼴 (u, v, 폭, 높이) 입니다. 폭이 음수면 좌우가 뒤집힌 프레임입니다.
     */
    struct SpriteClipFrame
    {
        float4 _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        /** @brief 이 프레임을 보여 줄 시간(ms)입니다. 0 이하면 애니메이터의 프레임 속도를 씁니다(`getFrameDurationSeconds`). */
        int32 _durationMs{ 100 };
    };

    /** @brief 클립의 트랜스폼 키입니다(위치 · 회전을 시간에 따라 움직이는 선택 트랙). JSON 키는 "time" · "x" · "y" · "angleDeg" 입니다. */
    struct SpriteClipKey
    {
        float32 _time{ 0.0f };
        float2  _position{ 0.0f, 0.0f };
        float32 _angleDeg{ 0.0f };
    };

    /**
     * @struct SpriteClipAnimation
     * @brief 이름 붙은 프레임 구간 하나입니다(예: "run" = 프레임 4..7). 애니메이터의 `play( name )` 이 이 이름으로 구간을 찾습니다.
     */
    struct SpriteClipAnimation
    {
        string _name;
        int32  _firstFrame{ 0 };
        int32  _frameCount{ 0 };
        uint8  _bLoop{ SW_TRUE };
    };

    /**
     * @class SpriteClipAsset
     * @brief `.sprite.json` 하나입니다. 에디터(SpriteClipPanel)가 쓰고 런타임(SpriteComponent · SpriteAnimatorComponent)이 읽는 **한 벌의 파서**입니다.
     * @details 형식(키 이름)은 에디터가 처음부터 쓰던 그대로입니다:
     *          `{ "atlas": 경로, "frames": [ { "u", "v", "w", "h", "durationMs" } ], "transformKeys": [ { "time", "x", "y", "angleDeg" } ] }`.
     *          여기에 선택 배열 `"animations": [ { "name", "start", "count", "loop" } ]` 이 붙습니다. 없는 옛 파일은 **프레임 전체가 이름 없는
     *          애니메이션 하나**입니다(`findFrameRange`). 예전에는 에디터만 이 형식을 알았고(`EditorSpriteClipData`) 런타임 타입이 없어서, 클립을
     *          만들어도 게임에서 읽는 곳이 없었습니다.
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
        [[nodiscard]] bool parseJson( string_view json );
        /** @brief JSON 본문을 만듭니다(들여쓰기 2). 애니메이션이 없으면 "animations" 키를 쓰지 않습니다(옛 파일과 바이트까지 같습니다). */
        string toJson() const;
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
         * @details 이름 붙은 애니메이션이 하나도 없는 클립(옛 파일)은 **어떤 이름이든 프레임 전체**를 반복 구간으로 답합니다 — 클립 하나가 곧
         *          애니메이션 하나이던 시절의 뜻입니다. 이름 붙은 것이 있는데 그 이름이 없으면 false 입니다.
         */
        bool findFrameRange( string_view name, SpriteClipAnimation& outRange ) const;

        /**
         * @brief 경로의 클립을 **나눠 받습니다**. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다.
         * @details 표는 약한 참조입니다(`MeshUtil::acquirePrimitive` 와 같은 모양) — 같은 클립을 쓰는 스프라이트 백 개가 파일을 백 번 읽지 않고,
         *          마지막 사용자가 놓으면 사라집니다. 비동기 씬 로드의 워커에서 불려도 됩니다(잠급니다).
         */
        static shared_ptr<const SpriteClipAsset> acquireShared( string_view path );
        /**
         * @brief 나눠 준 클립을 파일에서 **제자리로** 다시 읽습니다(에디터 핫 리로드 — 언리얼의 재임포트). 쥔 쪽 모두가 새 내용을 봅니다.
         * @details 표는 약한 참조라, 쥔 스프라이트가 하나라도 있으면 파일을 고쳐도 옛 내용이 나왔다(플레이 중 저장이 살아 있는 스프라이트에 닿지 않았다).
         *          틱 밖(게임 스레드)에서만 부릅니다 — 틱 중의 스프라이트가 읽는 내용을 바꿉니다. 그 뒤 `SpriteComponent::refreshFromClip` 으로 프레임을 다시 맞춥니다.
         * @return 그 경로의 클립을 누가 쥐고 있었고 다시 읽었으면 true. 쥔 쪽이 없으면(다음에 읽을 때 새 내용이다) · 읽을 수 없으면 false(옛 내용 그대로)입니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path );

        string                      _atlasPath;     ///< 아틀라스 텍스처 경로("atlas")입니다
        vector<SpriteClipFrame>     _listFrame;     ///< 프레임("frames")입니다
        vector<SpriteClipKey>       _listKey;       ///< 트랜스폼 키("transformKeys")입니다
        vector<SpriteClipAnimation> _listAnimation; ///< 이름 붙은 구간("animations")입니다. 옛 파일은 비어 있습니다

    private:
        /** @brief 파싱된 루트에서 내용을 읽습니다. 루트가 객체가 아니면 false 입니다. */
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );
    };
} // namespace sw
