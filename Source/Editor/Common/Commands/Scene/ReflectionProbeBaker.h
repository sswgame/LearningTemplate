/**
 * @file ReflectionProbeBaker.h
 * @brief 반사 프로브의 GPU 베이크입니다. 프로브 위치에서 씬을 면 여섯으로 그려(HDR) 접고 거칠기 단계마다 거른 `.reflprobe` 데이터를 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"
#include "Engine/Renderer/Capture/SceneCapture.h"

namespace sw
{
    class CameraComponent;
    class ReflectionProbeComponent;
    class RenderThread;
    class Scene;

    /** @struct ReflectionProbeFaceCapture @brief GPU 에서 읽은 면 하나입니다 - `LitColor`(RGBA16F) 바이트 그대로와 그린 카메라입니다. */
    struct ReflectionProbeFaceCapture
    {
        vector<uint8> _bytes;         ///< 행 0 이 위, 행마다 `_rowBytes`
        float4x4      _viewProj{};    ///< 그린 카메라의 뷰-투영(`ReflectionProbeFace::_viewProj`)
        float3        _forward{};     ///< 그 카메라가 보는 방향
        uint32        _rowBytes{ 0 }; ///< 한 행의 바이트(읽기 레이아웃)
        uint32        _size{ 0 };     ///< 한 변 픽셀 수
    };

    /**
     * @struct ReflectionProbeCapture
     * @brief 프로브 하나를 GPU 로 찍은 결과입니다(면 여섯 + 접기 인자). 접기 · 거르기(`ReflectionProbeBaker::makeProbeData`)는 GPU 가 없어도 되는 CPU 일이라 워커가 한다.
     */
    struct ReflectionProbeCapture
    {
        ReflectionProbeFaceCapture _arrFace[ReflectionProbeAsset::kFaceCount];
        float3                     _capturePosition{};
        uint32                     _resolution{ 0 };  ///< 옥타헤드럴 한 변(프로브의 칸)
        uint32                     _sampleCount{ 0 }; ///< 거르기 표본 수(프로브의 칸)
    };

    /**
     * @class ReflectionProbeBaker
     * @brief 프로브 위치에 90° 정사각 카메라를 두고 면마다 디퍼드 파이프라인으로 그려 조명 결과(`LitColor`, 후처리 앞의 HDR)를 읽고, CPU 로 접고 거릅니다(`ReflectionProbeAsset`).
     * @details 렌더러는 자기 것(`SceneCapture`)이라 주 렌더러의 상태를 건드리지 않는다(`PortraitStudio` 와 같은 격리). 씬은 찍을 씬 그대로다 — 찍는 동안만
     *          카메라 오브젝트 하나를 씬에 두고 활성 게임 카메라로 고른 뒤 지운다(지우면 직접 고른 카메라가 풀려 씬이 레지스트리 규칙으로 다시 고른다 — 찍기 전에
     *          `setActiveGameCamera` 로 직접 고른 카메라가 있었으면 그 선택은 풀린다).
     *          포워드가 아니라 디퍼드인 까닭: 포워드 파이프라인의 SceneColor 는 RGBA8 이라 1 을 넘는 빛이 잘린다. 디퍼드의 LitColor 는 RGBA16F 이고 하늘 · 반투명까지 들어간 뒤다.
     *
     *          찍기(`captureProbe`)와 접기(`makeProbeData`)를 나눈다 - 찍기는 GPU 를 기다리는 디바이스 스레드 일이고, 풀기 · 접기 · 거르기는 CPU 일이라 에디터가 워커에 맡긴다.
     *
     *          스레드: 게임 스레드에서 부릅니다. 찍을 카메라는 게임 스레드가 두고, 그리기 · 읽기는 `SceneCapture` 가 렌더 스레드 명령으로 한다.
     */
    class ReflectionProbeBaker
    {
    public:
        ReflectionProbeBaker();
        ~ReflectionProbeBaker();

        ReflectionProbeBaker( const ReflectionProbeBaker& )            = delete;
        ReflectionProbeBaker& operator=( const ReflectionProbeBaker& ) = delete;

        /** @brief 렌더 스레드에서 찍기 렌더러(엔진 기본 디퍼드 파이프라인)를 만듭니다. */
        [[nodiscard]] bool initialize( RenderThread& renderThread );
        void               shutdown();

        /**
         * @brief 프로브 하나를 면 여섯으로 찍어 @p outCapture 에 담습니다(GPU 일만 - 접기는 `makeProbeData`). 실패하면 그 까닭입니다.
         * @details 면 크기 · 옥타헤드럴 크기 · 표본 수는 프로브의 칸이다(`ReflectionProbeComponent`). 찍은 위치는 프로브의 월드 위치다.
         */
        [[nodiscard]] expected<void> captureProbe( Scene& scene, const ReflectionProbeComponent& probe, ReflectionProbeCapture& outCapture );

        /**
         * @brief @p renderThread 로 찍기 렌더러를 만들고 프로브마다 `captureProbe` 합니다. @p outListCapture 는 @p listProbe 와 같은 순서이고,
         *        실패한 프로브의 자리는 빈 결과입니다. 처음 실패한 프로브에서 멈추고 그 까닭을 돌려줍니다.
         */
        [[nodiscard]] static expected<void> captureProbes( RenderThread& renderThread, Scene& scene, const vector<ReflectionProbeComponent*>& listProbe,
                                                           vector<ReflectionProbeCapture>& outListCapture );

        /**
         * @brief 찍은 면을 선형 RGB 로 풀고(half → float) 접고 거칠기 단계마다 걸러 @p outData 에 담습니다. GPU · 씬 · 서비스를 쓰지 않아 아무 스레드에서 불러도 된다(워커).
         * @return 거르기가 거절하면 그 까닭입니다.
         */
        [[nodiscard]] static expected<void> makeProbeData( const ReflectionProbeCapture& capture, ReflectionProbeAsset& outData );

    private:
        /** @brief @p camera 로 몇 장 그린 뒤 `LitColor` 를 읽어 @p outFace 의 바이트 · 행 간격에 담습니다(풀지 않는다). */
        [[nodiscard]] expected<void> captureFace( Scene& scene, CameraComponent& camera, uint32 size, ReflectionProbeFaceCapture& outFace );

    private:
        SceneCapture _capture; ///< 그리기 · 읽기(렌더 스레드 명령)
    };
} // namespace sw
