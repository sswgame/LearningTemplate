/**
 * @file SpriteClipModel.h
 * @brief 스프라이트 클립 창의 모델 — 아틀라스 경로, 프레임 · 변형 키 · 애니메이션 구간 목록과 고른 항목, 아틀라스 셀 나누기, 읽기 · 쓰기 · 되돌리기 글, 상태 줄. ImGui 를 모릅니다.
 * @details 패널(`SpriteClipPanel`)은 그리기 · 아틀라스 썸네일 · 입력 읽기만 하고 문서 dirty · 되돌리기 기록을 남긴다 — 타일맵 페인터(`TileMapModel`)와 같은 선이다
 *          (유니티 Sprite Editor 의 데이터 제공자와 창이 나뉜 것과 같다). 편집한 명령은 true 를 반환한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"

namespace sw::editor
{
    /**
     * @class SpriteClipModel
     * @brief 스프라이트 클립 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class SpriteClipModel
    {
    public:
        /** @brief 아틀라스를 나눌 열 · 행 수의 범위입니다. */
        static constexpr int32 kMaxSliceCount = 64;

        /** @brief 미리보기에서 시간이 없는 프레임이 머무는 시간(초)입니다. 애니메이터의 기본 프레임 속도(12 fps, `SpriteAnimatorComponent::_frameRate`)와 같습니다. */
        static constexpr float32 kPreviewFrameSeconds = 1.0f / 12.0f;

        /** @brief 에디터 설정의 기본 아틀라스를 채우고 빈 프레임 하나로 시작합니다. */
        SpriteClipModel();

        /**
         * @brief 문서 경로 @p path 를 저장할 클립 경로로 바꿉니다. 이미지면 같은 자리의 `<이름>.sprite.xml` 이고, 아니면 그대로입니다.
         * @details 포커스한 이미지는 아틀라스로 열 수 있어(종류 규칙의 Fallback) 열린 문서 경로가 이미지일 수 있다. 그대로 쓰면 이미지를 클립 글로 덮어썼다.
         */
        [[nodiscard]] static string computeClipPath( string_view path );

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 글
        // ------------------------------------------------------------------------------
        /** @brief @p path 를 읽습니다. 파일이 없거나 이미지면 새 클립(이미지는 그 아틀라스, 빈 프레임 하나)으로 시작합니다 — 앞 문서의 내용이 남지 않습니다. 실패 이유는 상태 줄에 남깁니다. */
        ToolAssetLoadResult loadDocument( string_view path );
        /** @brief 지금 클립을 @p path(비면 기본 문서, `loadDocument` 와 같다)에 쓰고 결과를 상태 줄에 남깁니다. 성공하면 true 입니다(dirty 해제 · 되돌리기 기준은 패널이 한다). 이미지 경로는 거절합니다. */
        [[nodiscard]] bool saveDocument( string_view path );
        /** @brief 되돌리기 기록용 클립 글입니다. */
        string captureText() const;
        /** @brief 되돌리기 기록 글로 클립을 되돌립니다. 고른 항목은 범위 안에서 그대로입니다. 읽지 못하면 빈 클립을 보입니다. */
        void applyText( string_view text );

        // ------------------------------------------------------------------------------
        // 2) 프레임 · 변형 키
        // ------------------------------------------------------------------------------
        const vector<SpriteClipFrame>& getFrames() const { return _clip._listFrame; }
        int32                          getSelectedFrame() const { return _selectedFrame; }
        void                           selectFrame( int32 frameIndex ) { _selectedFrame = frameIndex; }
        /** @brief 고른 프레임입니다. 고른 것이 없으면 nullptr 입니다(입력란이 바로 고친다). */
        [[nodiscard]] SpriteClipFrame* editSelectedFrame();
        /** @brief 빈 프레임을 끝에 더하고 고릅니다. */
        void addFrame();
        /** @brief 고른 프레임을 지우고 애니메이션 구간을 그만큼 당깁니다. 프레임이 하나도 남지 않은 구간은 지웁니다(읽기가 버리는 구간을 남기지 않는다). 고른 것이 없으면 false 입니다. */
        bool removeSelectedFrame();
        /** @brief 고른 프레임 바로 뒤에 복사본을 넣고 고릅니다(Godot SpriteFrames 의 프레임 복제). 구간 안에 들어가면 구간이 늘어납니다. 고른 것이 없으면 false 입니다. */
        bool duplicateSelectedFrame();
        /** @brief 고른 프레임을 @p delta 칸 옮깁니다(재생 순서 바꾸기). 구간은 번호라 그대로입니다. 끝을 넘으면 false 입니다. */
        bool moveSelectedFrame( int32 delta );
        /** @brief 격자의 모든 칸을 왼쪽 위부터 프레임으로 더합니다(유니티 Sprite Editor 의 Slice, Godot 의 Add Frames from Sprite Sheet). 마지막 프레임을 고릅니다. */
        void addFramesFromGrid();
        /** @brief 고른 프레임의 픽셀 테두리를 확정합니다. 음수는 0 으로 올리고, 아틀라스 크기를 몰라 쓸 수 없는 테두리면 비우고 상태 줄에 알리며 false 입니다(읽기가 오류로 버리는 값을 저장하지 않는다). */
        bool commitSelectedFrameBorder();

        const vector<SpriteClipKey>& getKeys() const { return _clip._listKey; }
        int32                        getSelectedKey() const { return _selectedKey; }
        void                         selectKey( int32 keyIndex ) { _selectedKey = keyIndex; }
        /** @brief 고른 변형 키입니다. 고른 것이 없으면 nullptr 입니다. */
        [[nodiscard]] SpriteClipKey* editSelectedKey();
        void                         addKey();
        bool                         removeSelectedKey();

        // ------------------------------------------------------------------------------
        // 3) 애니메이션 구간
        // ------------------------------------------------------------------------------
        const vector<SpriteClipAnimation>& getAnimations() const { return _clip._listAnimation; }
        int32                              getSelectedAnimation() const { return _selectedAnimation; }
        /** @brief 구간 @p animationIndex 를 고르고 이름 입력란을 그 이름으로 둡니다(범위 밖이면 고름을 풀고 비운다). */
        void selectAnimation( int32 animationIndex );
        /** @brief 고른 구간입니다. 고른 것이 없으면 nullptr 입니다. */
        [[nodiscard]] SpriteClipAnimation* editSelectedAnimation();
        /** @brief 지금 프레임 수를 덮는 구간(겹치지 않는 `anim<n>`)을 더하고 고릅니다. 프레임이 없으면 상태 줄에 알리고 false 입니다(길이 0 구간은 읽기가 버린다). */
        bool addAnimation();
        bool removeSelectedAnimation();
        /** @brief 고른 구간의 이름 입력란입니다. 입력을 마치면 `commitAnimationName` 으로 구간에 옮긴다. */
        fixed_string<constant::buffer::kMax64>& editAnimationName() { return _animationName; }
        /** @brief 이름 입력란을 고른 구간에 옮깁니다. 비었거나 다른 구간과 겹치면 입력란을 되돌리고 상태 줄에 알리며 false 입니다(`play( name )` 이 찾지 못하거나 읽기가 버린다). */
        bool commitAnimationName();
        /** @brief 고른 구간의 시작 · 길이를 프레임 목록 안으로 묶습니다(길이는 1 이상). 입력을 마칠 때 부릅니다. 고른 것이 없으면 false 입니다. */
        bool commitAnimationRange();

        // ------------------------------------------------------------------------------
        // 4) 아틀라스
        // ------------------------------------------------------------------------------
        fixed_string<constant::buffer::kMax256>& editAtlasPath() { return _atlasPath; }
        const utf8*                              getAtlasPath() const { return _atlasPath.c_str(); }
        bool                                     hasAtlasPath() const { return _atlasPath.empty() == false; }
        /** @brief 아틀라스 픽셀 크기 입력란입니다. 입력을 마치면 `commitAtlasPixelSize` 로 클립에 옮긴다. */
        float2& editAtlasPixelSize() { return _atlasPixelSizeInput; }
        /** @brief 입력란을 클립의 아틀라스 크기로 둡니다. 음수는 0 으로 올립니다. 테두리가 있는 프레임이 쓸 수 없게 되면 입력란을 되돌리고 상태 줄에 알리며 false 입니다. 바뀌지 않아도 false 입니다. */
        bool   commitAtlasPixelSize();
        int32& editSliceColumns() { return _sliceColumns; }
        int32& editSliceRows() { return _sliceRows; }
        int32  getSliceColumns() const { return _sliceColumns; }
        int32  getSliceRows() const { return _sliceRows; }
        /** @brief 열 · 행 입력란을 1..`kMaxSliceCount` 로 묶습니다. 입력란을 그린 뒤 부릅니다. */
        void clampSliceGrid();
        /** @brief `pickAtlasCell` 이 한 일입니다(되돌리기 기록 이름이 갈린다). */
        enum class AtlasPickResult : uint8
        {
            None = 0,    ///< 셀 밖이라 아무것도 하지 않았다
            EditedFrame, ///< 고른 프레임의 uv 를 바꿨다
            AddedFrame   ///< 새 프레임을 더해 그 uv 로 두었다
        };
        /** @brief 아틀라스의 uv(@p u, @p v) 셀을 고른 프레임의 uv 사각형으로 넣습니다. @p bAddFrame 이거나 고른 프레임이 없으면 새 프레임을 더해 넣습니다. */
        AtlasPickResult pickAtlasCell( float32 u, float32 v, bool bAddFrame );

        const string& getStatus() const { return _status; }

        // ------------------------------------------------------------------------------
        // 5) 미리보기 — 고른 구간(없으면 프레임 전체)을 런타임 애니메이터와 같은 프레임 시간으로 돌린다(Godot SpriteFrames 의 재생)
        // ------------------------------------------------------------------------------
        bool isPreviewPlaying() const { return _bPreviewPlaying == SW_TRUE; }
        /** @brief 재생을 켜고 끕니다. 켤 때 처음부터 돌립니다. */
        void togglePreview();
        /** @brief 재생 중이면 시간을 @p deltaSeconds 흘립니다. 반복 구간은 돌고, 반복이 아니면 끝에서 멈춥니다. */
        void advancePreview( float32 deltaSeconds );
        /** @brief 지금 미리보기에 보일 프레임 번호입니다. 프레임이 없으면 -1 입니다. */
        int32 getPreviewFrame() const;

    private:
        /** @brief 파일과 같은 내용의 새 클립입니다. 아틀라스는 @p atlasPath, 빈 프레임 하나입니다. */
        static SpriteClipAsset makeNewClip( string_view atlasPath );
        SpriteClipAsset        captureClipData() const;
        /** @brief 클립을 편집 상태로 받습니다. 아틀라스 칸도 그 클립의 것입니다(비었으면 비운다 — 앞 문서의 아틀라스가 이 문서에 저장되지 않는다). 고름은 범위 안으로 묶습니다. */
        void applyClip( SpriteClipAsset&& clip );
        /** @brief 미리보기가 도는 구간입니다. 고른 구간, 없으면 프레임 전체입니다. */
        void findPreviewRange( int32& outFirstFrame, int32& outFrameCount, bool& outLoop ) const;
        /** @brief 프레임 @p frameIndex 의 미리보기 시간(초)입니다. */
        float32 getPreviewFrameSeconds( int32 frameIndex ) const { return _clip.getFrameDurationSeconds( frameIndex, kPreviewFrameSeconds ); }

    private:
        SpriteClipAsset                         _clip; ///< 편집 중인 클립 하나다 — 칸을 따로 옮겨 담으면 새 칸(`_atlasPixelSize`)이 저장에서 빠진다. 아틀라스 경로만 입력란(`_atlasPath`)에서 저장할 때 옮긴다
        fixed_string<constant::buffer::kMax256> _atlasPath;
        string                                  _status;
        fixed_string<constant::buffer::kMax64>  _animationName;       ///< 고른 애니메이션 이름의 편집 칸입니다. 고름이 바뀌면 `selectAnimation` 이 함께 바꾼다
        float2                                  _atlasPixelSizeInput; ///< 아틀라스 픽셀 크기의 편집 칸입니다
        int32                                   _selectedFrame;
        int32                                   _selectedKey;
        int32                                   _selectedAnimation;
        int32                                   _sliceColumns;   ///< 아틀라스를 나눌 열 수(셀 고르기)
        int32                                   _sliceRows;      ///< 아틀라스를 나눌 행 수
        float32                                 _previewSeconds; ///< 미리보기 구간 시작부터 흐른 시간(초)
        uint8                                   _bPreviewPlaying : 1;
    };
} // namespace sw::editor
