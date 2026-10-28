/**
 * @file EditorViewTargetUtil.h
 * @brief 씬 뷰와 게임 뷰 렌더 타깃의 판정을 담당합니다. 호스트에 무엇을 요청할지, 크기를 다시 맞출지, 게임 뷰 이미지 사각형을 어떻게 잡을지를 정합니다.
 * @details ImGui 없이 계산만 하므로 `Test/EditorTest` 가 테스트한다. 패널은 그린 크기를 이 판정에 넘기고, 셸(`ImGuiEditor`)은 보이는 뷰만 호스트에 알린다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 에디터가 호스트에 그려 달라는 뷰의 종류입니다. */
    enum class EditorViewKind : uint8
    {
        Scene = 0, ///< 에디터 카메라, 격자, 기즈모, 피킹을 담는다(Scene 패널)
        Game,      ///< 활성 씬의 게임 카메라 출력과 화면 UI 를 담는다(Game 패널)
        Count
    };

    /** @brief 게임 뷰 이미지의 화면 비율입니다. */
    enum class EditorGameViewAspect : uint8
    {
        Free = 0,  ///< 패널 전체를 쓴다
        Ratio16x9, ///< 16:9 비율이며 남는 쪽은 레터박스로 둔다
        Count
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패널 안에서 이미지가 차지하는 사각형입니다. 패널 내용 영역의 왼쪽 위가 기준입니다. */
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
     * @brief 뷰 렌더 타깃 판정 함수 모음입니다. 유니티의 Scene 뷰와 Game 뷰처럼 보이는 패널의 뷰만 그립니다.
     */
    struct EditorViewTargetUtil
    {
        /**
         * @brief 이번 프레임에 씬 뷰 렌더 타깃을 호스트에 알릴지 판정합니다.
         * @details 씬 뷰가 보이면 알린다. 두 패널이 모두 보이지 않을 때(닫힘, 접힘, 다른 탭)도 씬 뷰 렌더 타깃을 알린다. 알릴 렌더 타깃이 없으면 호스트가
         *          백버퍼에 그려서 에디터 UI 밑에 깔리기 때문이다. 게임 뷰만 보이면 알리지 않는다. 그리지 않는다.
         */
        static bool shouldRequestSceneView( bool bSceneViewDrawn, bool bGameViewDrawn ) { return bSceneViewDrawn || bGameViewDrawn == false; }
        /** @brief 이번 프레임에 게임 뷰 렌더 타깃을 호스트에 알릴지 판정합니다. 보일 때만 알립니다. */
        static bool shouldRequestGameView( bool bGameViewDrawn ) { return bGameViewDrawn; }
        /**
         * @brief 현재 렌더 타깃 크기를 @p targetWidth x @p targetHeight 로 다시 만들어야 하는지 판정합니다.
         * @details 1픽셀 차이는 무시한다. 도크 분할의 반올림이 프레임마다 1픽셀씩 오가면 렌더 타깃을 매 프레임 다시 만들게 된다. 목표가 0 이면 만들지 않는다.
         */
        static bool needsResize( uint32 currentWidth, uint32 currentHeight, uint32 targetWidth, uint32 targetHeight );
        /**
         * @brief 게임 뷰 이미지 사각형을 정합니다. @p available 은 패널 내용 영역 크기입니다.
         * @details `Free` 는 영역 전체를 쓰고, `Ratio16x9` 는 영역 안에 16:9 로 가장 크게 넣어 가운데에 둔다. 크기는 렌더 타깃 크기와 같도록 정수 픽셀로 버림한다.
         */
        static EditorViewRect fitViewImage( const float2& available, EditorGameViewAspect aspect );
        /** @brief 화면 비율 선택에 보이는 이름을 반환합니다. */
        static const utf8* getAspectLabel( EditorGameViewAspect aspect );
    };
} // namespace sw::editor
