/**
 * @file EditorViewTargetUtil.h
 * @brief 씬 뷰 · 게임 뷰 렌더 타깃의 판정(호스트에 무엇을 요청하나 · 크기를 다시 맞추나 · 게임 뷰 이미지 사각형)입니다.
 * @details ImGui 없이 계산만 해서 `Test/EditorTest` 가 시험합니다. 패널은 그린 크기를 이 판정에 넘기고, 셸(`ImGuiEditor`)은 보인 뷰만 호스트에 알립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 에디터가 호스트에 그려 달라는 뷰의 종류입니다. */
    enum class EditorViewKind : uint8
    {
        Scene = 0, ///< 에디터 카메라 · 격자 · 기즈모 · 피킹(Scene 패널)
        Game,      ///< 활성 씬의 게임 카메라 출력 · 화면 UI(Game 패널)
        Count
    };

    /** @brief 게임 뷰 이미지의 화면 비율입니다. */
    enum class EditorGameViewAspect : uint8
    {
        Free = 0,  ///< 패널 전체
        Ratio16x9, ///< 16:9 — 남는 쪽은 레터박스
        Count
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패널 안에서 이미지가 차지하는 사각형입니다(패널 내용 영역의 왼쪽 위 기준). */
    struct EditorViewRect
    {
        float2 _offset{};
        float2 _size{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorViewTargetUtil
     * @brief 뷰 렌더 타깃 판정 함수 모음입니다. 유니티의 Scene · Game 뷰처럼 보이는 패널의 뷰만 그립니다.
     */
    struct EditorViewTargetUtil
    {
        /**
         * @brief 이번 프레임에 씬 뷰 RT 를 호스트에 알릴지입니다.
         * @details 씬 뷰가 보이면 알립니다. 두 패널이 모두 안 보이면(닫힘 · 접힘 · 다른 탭)도 씬 뷰 RT 를 알립니다 — 알릴 RT 가 없으면 호스트가
         *          백버퍼에 그려 에디터 UI 밑에 깔기 때문입니다. 게임 뷰만 보이면 알리지 않습니다(그리지 않는다).
         */
        static bool shouldRequestSceneView( bool bSceneViewDrawn, bool bGameViewDrawn ) { return bSceneViewDrawn || bGameViewDrawn == false; }
        /** @brief 이번 프레임에 게임 뷰 RT 를 호스트에 알릴지입니다 — 보일 때만입니다. */
        static bool shouldRequestGameView( bool bGameViewDrawn ) { return bGameViewDrawn; }
        /**
         * @brief 지금 RT 크기를 @p targetWidth x @p targetHeight 로 다시 만들어야 하는지입니다.
         * @details 한 픽셀 차이는 무시합니다(도크 분할의 반올림이 프레임마다 1 픽셀씩 오가면 RT 를 매 프레임 다시 만든다). 목표가 0 이면 만들지 않습니다.
         */
        static bool needsResize( uint32 currentWidth, uint32 currentHeight, uint32 targetWidth, uint32 targetHeight );
        /**
         * @brief 게임 뷰 이미지 사각형을 정합니다. @p available 은 패널 내용 영역 크기입니다.
         * @details `Free` 는 영역 전체, `Ratio16x9` 는 영역 안에 16:9 로 가장 크게 넣고 가운데에 둡니다. 크기는 정수 픽셀로 내립니다(RT 크기와 같게).
         */
        static EditorViewRect fitViewImage( const float2& available, EditorGameViewAspect aspect );
        /** @brief 화면 비율 고르기에 보이는 이름입니다. */
        static const utf8* getAspectLabel( EditorGameViewAspect aspect );
    };
} // namespace sw::editor
