/**
 * @file VolumetricFogViewModel.h
 * @brief 볼륨 안개 창의 ViewModel — 주 화면 볼륨 상태 · 진단 보기 변수 · 씬의 안개 오브젝트를 값으로 모으고(`gather`), 진단 보기 · 단면 조각 · 안개 고르기 명령을 받습니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 진단 보기는 개발 빌드 변수(`gv_fogDebugView` · `gv_fogDebugSlice`)라 저장 · 되돌리기에 남지 않는다. 저작 값은 씬의 `FogComponent` 가 정본이라
 *          이 창은 사본을 두지 않고 안개 오브젝트를 골라 인스펙터가 고치게 한다(고르기도 문서 데이터가 아니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"

namespace sw::editor
{
    /** @brief 볼륨 안개 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct VolumetricFogViewState
    {
        FogVolumeState _volumeState{};    ///< 지난 주 화면 볼륨 안개 상태(`FrameFog::getLastMainVolumeState`)
        string         _debugView{};      ///< `gv_fogDebugView` 의 지금 글(열거 이름)
        int32          _slice{ 0 };       ///< `gv_fogDebugSlice` 의 지금 값
        string         _fogObjectName{};  ///< 씬의 첫 안개 오브젝트 이름
        uint64         _fogObjectID{ 0 }; ///< 그 오브젝트 id(없으면 0)
    };

    /** @brief 볼륨 안개 창의 ViewModel 입니다(상태 없음 — 정본은 전역 변수와 씬이다). */
    class VolumetricFogViewModel
    {
    public:
        /** @brief 창이 그릴 값을 @p outState 에 채웁니다. */
        void gather( VolumetricFogViewState& outState ) const;
        /** @brief 진단 보기를 @p view 로 바꿉니다(개발 변수). */
        void setDebugView( FogDebugView view );
        /** @brief 진단 단면의 거리 조각을 @p slice 로 바꿉니다(개발 변수). */
        void setSlice( int32 slice );
        /** @brief 활성 씬의 오브젝트 @p objectID 를 고릅니다(인스펙터가 안개 값을 보인다). 없는 id 면 아무것도 하지 않습니다. */
        void selectFogObject( uint64 objectID );
    };
} // namespace sw::editor
