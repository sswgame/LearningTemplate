/**
 * @file SpriteClipAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 스프라이트 클립(`.sprite.xml`) 에셋입니다. 아틀라스, 프레임(UV 사각형과 시간), 이름 붙은 애니메이션을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimationPlayback.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct SpriteClipFrame
     * @brief 클립의 프레임 하나입니다. 아틀라스 안의 UV 사각형과 보여 줄 시간을 담습니다.
     * @details UV 는 텍스처 크기에 대한 비율(0..1)이고 머티리얼 uvRect 와 같은 형태 (u, v, 폭, 높이)다. 폭이 음수면 좌우가 뒤집힌 프레임이다.
     */
    REFLECT()
    struct SW_API SpriteClipFrame
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Frame rectangle in the atlas (u, v, width, height) as fractions of the texture; a negative width mirrors the frame" )
        float4 _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        /**
         * @brief 9-슬라이스 테두리 (왼쪽, 아래, 오른쪽, 위)입니다. 이 프레임에 대한 비율(0..1)이며, 모두 0 이면 테두리가 없습니다. 런타임이 읽는 값입니다.
         * @details 스프라이트의 자연 크기가 1 × 1 이라 비율이 곧 모서리의 월드 크기다(`SlicedSpriteDesc`). 파일에 적는 값은 `_borderPixels` 이고,
         *          이 값은 그것을 프레임의 픽셀 크기로 나눈 것이다(`SpriteClipAsset::rebuildFrameBorders`). 파일에는 적지 않는다.
         */
        float4 _border{ 0.0f, 0.0f, 0.0f, 0.0f };
        /**
         * @brief 9-슬라이스 테두리 (왼쪽, 아래, 오른쪽, 위)를 아틀라스 **픽셀**로 적은 값입니다. 모두 0 이면 테두리가 없습니다.
         * @details 픽셀을 비율로 바꾸려면 아틀라스 크기(`SpriteClipAsset::_atlasPixelSize`)가 있어야 한다. 유니티의 Sprite Border, Godot 의 patch margin 처럼 픽셀이다.
         */
        PROPERTY( Tooltip = "Nine-slice border (left, bottom, right, top) in atlas pixels; all zero is no border" )
        float4 _borderPixels{ 0.0f, 0.0f, 0.0f, 0.0f };
        /** @brief 이 프레임을 보여 줄 시간(ms)입니다. 0 이하면 애니메이터의 프레임 속도를 씁니다(`getFrameDurationSeconds`). */
        PROPERTY( Units = ms, Tooltip = "How long the frame shows (0 or less: the animator frame rate)" )
        int32 _durationMs{ 100 };

        /** @brief 테두리가 하나라도 있으면 true 입니다. */
        bool hasBorder() const { return _border._x > 0.0f || _border._y > 0.0f || _border._z > 0.0f || _border._w > 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SpriteClipKey
     * @brief 클립의 트랜스폼 키입니다. 위치와 회전을 시간에 따라 움직이는 선택 트랙입니다.
     * @details `_time` 은 **클립 타임라인**의 초다. 프레임 0 이 0 초에 시작하고 프레임마다 그 프레임의 시간만큼 흐른다
     *          (`computeFrameStartSeconds`). 이름 붙은 애니메이션은 그 구간의 시각에서 트랙을 읽는다. 값은 애니메이터가 움직이는
     *          스프라이트의 **로컬** 위치(x, y)와 Z 축 회전(라디안, 엔진의 각도 단위)이다(`SpriteAnimatorComponent`).
     */
    REFLECT()
    struct SW_API SpriteClipKey
    {
        REFLECT_BODY();

        PROPERTY( Units = s, Tooltip = "Clip timeline time of the key" )
        float32 _time{ 0.0f };
        PROPERTY( Tooltip = "Local position (x, y) of the sprite at this key" )
        float2 _position{ 0.0f, 0.0f };
        PROPERTY( Units = rad, Tooltip = "Rotation about Z at this key" )
        float32 _angle{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SpriteClipAnimation
     * @brief 이름 붙은 프레임 구간 하나입니다(예: "run" = 프레임 4..7). 애니메이터의 `play( name )` 이 이 이름으로 구간을 찾습니다.
     */
    REFLECT()
    struct SW_API SpriteClipAnimation
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Animation name the animator plays (unique in the clip, not empty)" )
        string _name;
        /** @brief 구간 시작 기준 시각(초)의 알림이다. 스켈레탈 클립의 알림과 같은 의미이며, 길이가 0 보다 크면 구간 알림이다. 이름이 빈 알림은 읽을 때 버린다. */
        PROPERTY( Tooltip = "Notifies at times from the start of the range" )
        vector<AnimationNotifyEvent> _listNotify;
        PROPERTY( Tooltip = "First frame of the range" )
        int32 _firstFrame{ 0 };
        PROPERTY( Tooltip = "Frame count of the range" )
        int32 _frameCount{ 0 };
        PROPERTY( Tooltip = "Loop the range" )
        uint8 _bLoop{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SpriteClipAsset
     * @brief `.sprite.xml` 하나입니다. 에디터(SpriteClipPanel)가 쓰고 런타임(SpriteComponent, SpriteAnimatorComponent)이 읽는 **하나의 파서**입니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigXMLElement` — 모르는 속성 · 자식 · 읽지 못한 값은 오류). 키는 PROPERTY 이름이 기준이다
     *          (`_atlasPath`, `_atlasPixelSize`, `_listFrame`, `_listKey`, `_listAnimation`). 적지 않은 칸은 구조체 초기값이다.
     *          `_listAnimation` 이 빈 클립은 **프레임 전체가 이름 없는 애니메이션 하나**다(`findFrameRange`).
     *          읽은 뒤 의미만 따로 본다: 프레임 밖 구간은 자르고 알리며, 이름이 비거나 길이가 0 인 구간과 이름이 빈 알림은 버린다.
     */
    REFLECT()
    class SW_API SpriteClipAsset
    {
    public:
        REFLECT_BODY();

        /** @brief 스프라이트 클립 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".sprite.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SpriteClipAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SpriteClipAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 빈 클립을 만듭니다. */
        SpriteClipAsset() = default;

        /** @brief 리소스 id 의 클립을 읽습니다. OS 경로는 받지 않습니다(`loadFromDiskFile`). 읽지 못하면 false 를 반환하고 내용은 비어 있습니다. */
        [[nodiscard]] bool loadFromFile( const AssetID& assetID );
        /** @brief **디스크 파일**의 클립을 읽습니다. 리소스 id 로 풀지 않습니다(에디터 문서 · 도구 · 테스트용). 실패 규칙은 `loadFromFile` 과 같습니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief XML 본문을 읽습니다. 구문이 틀렸거나, 모르는 속성 · 자식 · 읽지 못한 값이 있으면 false 이고 내용은 비어 있습니다. */
        [[nodiscard]] bool parseXML( string_view xml );
        /** @brief XML 본문을 만듭니다(모든 PROPERTY). */
        string toXML() const;
        /** @brief 내용을 비웁니다. */
        void clear();

        /** @brief 프레임 수입니다. */
        int32 getFrameCount() const { return static_cast<int32>( _listFrame.size() ); }
        /** @brief 프레임 하나입니다. 범위 밖이면 nullptr 입니다. */
        const SpriteClipFrame* findFrame( int32 frameIndex ) const;
        /** @brief 프레임을 보여 줄 시간(초)입니다. 프레임에 시간이 없거나(0 이하) 범위 밖이면 @p animatorFrameSeconds 입니다. */
        float32 getFrameDurationSeconds( int32 frameIndex, float32 animatorFrameSeconds ) const;
        /** @brief 이름으로 애니메이션을 찾습니다. 없으면 nullptr 입니다. */
        const SpriteClipAnimation* findAnimation( string_view name ) const;
        /**
         * @brief 이름의 프레임 구간(시작, 개수, 반복)을 찾습니다. 찾으면 true 를 반환하고 @p outRange 를 채웁니다(이름 필드는 건드리지 않습니다).
         * @details 이름 붙은 애니메이션이 하나도 없는 클립은 **어떤 이름이든 프레임 전체**를 반복 구간으로 반환한다. 클립 하나가 곧
         *          애니메이션 하나다. 이름 붙은 것이 있는데 그 이름이 없으면 false 를 반환한다.
         */
        [[nodiscard]] bool findFrameRange( string_view name, SpriteClipAnimation& outRange ) const;
        /** @brief 클립 타임라인에서 프레임 @p frameIndex 가 시작하는 시각(초)을 반환합니다. 앞 프레임들의 시간 합이며, 시간이 없는 프레임은 @p animatorFrameSeconds 로 셉니다. */
        float32 computeFrameStartSeconds( int32 frameIndex, float32 animatorFrameSeconds ) const;
        /** @brief 트랜스폼 키가 하나라도 있으면 true 입니다. */
        bool hasTransformKeys() const { return _listKey.empty() == false; }
        /**
         * @brief 트랜스폼 트랙을 클립 시각 @p clipSeconds 에서 샘플해 @p outKey 에 채웁니다. 키가 없으면 false 를 반환합니다.
         * @details 두 키 사이는 선형 보간이고, 첫 키 앞과 마지막 키 뒤는 그 키의 값을 유지한다(유니티 커브의 Clamp). 키는 시간 순서가
         *          아니어도 된다. 에디터는 추가한 순서로 쓴다. 시각이 같은 키가 여럿이면 목록의 앞쪽 것을 쓴다.
         */
        bool sampleTransformKey( float32 clipSeconds, SpriteClipKey& outKey ) const;
        /**
         * @brief 프레임마다 픽셀 테두리(`_borderPixels`)를 그 프레임의 픽셀 크기(UV 크기 × `_atlasPixelSize`)로 나눠 비율 테두리(`_border`)를 다시 채웁니다.
         * @details 읽을 때 부르고, 코드가 픽셀 테두리나 아틀라스 크기를 바꾼 뒤에도 부른다. 아틀라스 크기를 모르면(0) 테두리가 있는 프레임을
         *          알리고 비율을 0 으로 둔다 — 크기를 짐작해 틀린 모서리를 그리지 않는다. 비율은 0..1 로 자른다.
         * @return 아틀라스 크기가 없어 테두리를 버린 프레임 수입니다.
         */
        uint32 rebuildFrameBorders();

        PROPERTY( Tooltip = "Atlas texture path" )
        string _atlasPath;
        /** @brief 아틀라스의 픽셀 크기(폭, 높이)입니다. 픽셀 테두리를 비율로 바꿀 때 쓴다. 0 이면 모른다. */
        PROPERTY( Tooltip = "Atlas size in pixels (width, height); needed only by pixel borders, 0 is unknown" )
        float2 _atlasPixelSize{ 0.0f, 0.0f };
        PROPERTY( Tooltip = "Frames in play order" )
        vector<SpriteClipFrame> _listFrame;
        PROPERTY( Tooltip = "Optional transform track keys" )
        vector<SpriteClipKey> _listKey;
        /** @brief 이름 붙은 구간입니다. 비어 있으면 프레임 전체가 이름 없는 애니메이션 하나입니다. */
        PROPERTY( Tooltip = "Named frame ranges (empty: the whole clip is one animation)" )
        vector<SpriteClipAnimation> _listAnimation;

    private:
        /** @brief 읽은 값의 의미를 고칩니다(음수 크기 · 테두리는 오류, 프레임 밖 구간은 자르고 알림). 어긋나 읽을 수 없으면 false 입니다. */
        [[nodiscard]] bool resolveLoadedValues( string_view sourceLabel );
        /** @brief 읽은 본문을 클립으로 옮깁니다. 두 읽기 입구(`loadFromFile` · `loadFromDiskFile`)의 공통 단계입니다. */
        [[nodiscard]] bool loadText( const string& text, string_view sourceLabel );
    };
} // namespace sw
