/**
 * @file SpriteClipModel.h
 * @brief 스프라이트 클립 창의 모델 — 아틀라스 경로, 프레임 · 변형 키 · 애니메이션 구간 목록과 고른 항목, 아틀라스 셀 나누기, 읽기 · 쓰기 · 되돌리기 글, 상태 줄. ImGui 를 모릅니다.
 * @details 패널(`SpriteClipPanel`)은 그리기 · 아틀라스 썸네일 · 입력 읽기만 하고 문서 dirty · 되돌리기 기록을 남긴다 — 타일맵 페인터(`TileMapDocument`)와 같은 선이다
 *          (유니티 Sprite Editor 의 데이터 제공자와 창이 나뉜 것과 같다). 편집한 명령은 true 를 반환한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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

        /** @brief 에디터 설정의 기본 아틀라스를 채우고 빈 프레임 하나로 시작합니다. */
        SpriteClipModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 글
        // ------------------------------------------------------------------------------
        /** @brief @p path 를 읽습니다. 텍스처면 아틀라스 경로로만 둡니다. 실패 이유는 상태 줄에 남깁니다. */
        ToolAssetLoadResult loadDocument( string_view path );
        /** @brief 지금 클립을 @p path 에 씁니다. 성공하면 상태 줄에 알리고 true 입니다(dirty 해제 · 되돌리기 기준은 패널이 한다). 경로가 비었으면 false 입니다. */
        [[nodiscard]] bool saveDocument( string_view path );
        /** @brief 되돌리기 기록용 클립 글입니다. */
        string captureText() const;
        /** @brief 되돌리기 기록 글로 클립을 되돌립니다. 읽지 못하면 빈 클립을 보입니다. */
        void applyText( string_view text );

        // ------------------------------------------------------------------------------
        // 2) 프레임 · 변형 키
        // ------------------------------------------------------------------------------
        const vector<SpriteClipFrame>& getFrames() const { return _listFrame; }
        int32                          getSelectedFrame() const { return _selectedFrame; }
        void                           selectFrame( int32 frameIndex ) { _selectedFrame = frameIndex; }
        /** @brief 고른 프레임입니다. 고른 것이 없으면 nullptr 입니다(입력란이 바로 고친다). */
        [[nodiscard]] SpriteClipFrame* editSelectedFrame();
        /** @brief 빈 프레임을 끝에 더하고 고릅니다. */
        void addFrame();
        /** @brief 고른 프레임을 지웁니다. 고른 것이 없으면 false 입니다. */
        bool removeSelectedFrame();

        const vector<SpriteClipKey>& getKeys() const { return _listKey; }
        int32                        getSelectedKey() const { return _selectedKey; }
        void                         selectKey( int32 keyIndex ) { _selectedKey = keyIndex; }
        /** @brief 고른 변형 키입니다. 고른 것이 없으면 nullptr 입니다. */
        [[nodiscard]] SpriteClipKey* editSelectedKey();
        void                         addKey();
        bool                         removeSelectedKey();

        // ------------------------------------------------------------------------------
        // 3) 애니메이션 구간
        // ------------------------------------------------------------------------------
        const vector<SpriteClipAnimation>& getAnimations() const { return _listAnimation; }
        int32                              getSelectedAnimation() const { return _selectedAnimation; }
        /** @brief 구간 @p animationIndex 를 고르고 이름 입력란을 그 이름으로 둡니다(범위 밖이면 고름을 풀고 비운다). */
        void selectAnimation( int32 animationIndex );
        /** @brief 고른 구간입니다. 고른 것이 없으면 nullptr 입니다. */
        [[nodiscard]] SpriteClipAnimation* editSelectedAnimation();
        /** @brief 지금 프레임 수를 덮는 구간(`anim<n>`)을 더하고 고릅니다. */
        void addAnimation();
        bool removeSelectedAnimation();
        /** @brief 고른 구간의 이름 입력란입니다. 입력을 마치면 `commitAnimationName` 으로 구간에 옮긴다. */
        fixed_string<constant::kMaxBuffer64>& editAnimationName() { return _animationName; }
        /** @brief 이름 입력란을 고른 구간에 옮깁니다. 고른 것이 없으면 false 입니다. */
        bool commitAnimationName();

        // ------------------------------------------------------------------------------
        // 4) 아틀라스
        // ------------------------------------------------------------------------------
        fixed_string<constant::kMaxBuffer256>& editAtlasPath() { return _atlasPath; }
        const utf8*                            getAtlasPath() const { return _atlasPath.c_str(); }
        bool                                   hasAtlasPath() const { return _atlasPath.empty() == false; }
        int32&                                 editSliceColumns() { return _sliceColumns; }
        int32&                                 editSliceRows() { return _sliceRows; }
        int32                                  getSliceColumns() const { return _sliceColumns; }
        int32                                  getSliceRows() const { return _sliceRows; }
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

    private:
        SpriteClipAsset captureClipData() const;
        /** @brief 읽은(또는 되돌린) 클립으로 편집 상태를 바꿉니다. 아틀라스가 비어 있으면 지금 아틀라스를 그대로 둡니다. */
        void adoptClip( SpriteClipAsset&& clip );

    private:
        fixed_string<constant::kMaxBuffer256> _atlasPath;
        string                                _status;
        vector<SpriteClipFrame>               _listFrame;
        vector<SpriteClipKey>                 _listKey;
        vector<SpriteClipAnimation>           _listAnimation;
        fixed_string<constant::kMaxBuffer64>  _animationName; ///< 고른 애니메이션 이름의 편집 칸입니다. 고름이 바뀌면 `selectAnimation` 이 함께 바꾼다
        int32                                 _selectedFrame;
        int32                                 _selectedKey;
        int32                                 _selectedAnimation;
        int32                                 _sliceColumns; ///< 아틀라스를 나눌 열 수(셀 고르기)
        int32                                 _sliceRows;    ///< 아틀라스를 나눌 행 수
    };
} // namespace sw::editor
