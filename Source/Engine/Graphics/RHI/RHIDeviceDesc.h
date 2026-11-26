/**
 * @file RHIDeviceDesc.h
 * @brief RHI 장치를 만들 때 넘기는 서술체입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHISwapChainDesc.h"

namespace sw
{
    /**
     * @struct RHIDeviceDesc
     * @brief 장치를 기동할 때만 쓰는 설정입니다 — 주 스왑체인(`_swapChain`)과 장치 고유의 칸(어댑터 · 스테이징 링 · 검증 층 · DRED · 파이프라인 캐시)을 함께 든다.
     * @details 예전에는 이 칸들이 `RHISwapChainDesc` 에 들어 있어 창 스왑체인(`IRHIDevice::createSwapChain`)도 쓰지 않는 장치 칸을 날랐다.
     *          언리얼은 장치 기동 설정(`-d3ddebug` · 어댑터 선택)을 스왑체인 서술과 따로 둔다(기억에 의존한 비교). 실행마다 정하는 기동 값이라 리플렉션하지 않는다.
     */
    struct RHIDeviceDesc
    {
        RHISwapChainDesc _swapChain; ///< 주 스왑체인 서술

        /// @brief 업로드 스테이징 용량(바이트)입니다(`_rhi._stagingRingMegabytes`). GL · Vulkan 은 링 크기, DX11 은 GPU 가 아직 복사 중인 스테이징 텍스처의 합의 상한으로 읽고,
        ///        0 이면 그 백엔드가 초기화를 거절합니다. 실행마다 정하는 설정이라 리플렉션하지 않는다.
        uint64 _stagingRingBytes{ 0 };
        /// @brief 이 크기(바이트) 이하의 텍스처 업로드는 스테이징 없이 드라이버 복사 호출로 바로 올립니다(`_rhi._directUploadMaxKilobytes`, 지금은 DX11 만 읽는다). 리플렉션하지 않는다.
        uint64 _directUploadMaxBytes{ 0 };

        /// @brief 텍스처 되읽기 링의 용량(바이트)입니다(`_rhi._readbackRingMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 읽고, 0 이면 그 백엔드가 초기화를 거절합니다. 리플렉션하지 않는다.
        uint64 _readbackRingBytes{ 0 };
        /// @brief 링보다 큰 전송 한 장을 링 밖의 일회성 버퍼로 받는 상한(바이트)입니다(`_rhi._stagingSingleMaxMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 업로드 · 되읽기 링에 같이 쓰고,
        ///        0 이면 링보다 큰 한 장은 `Rejected` 입니다. 리플렉션하지 않는다.
        uint64 _stagingSoloMaxBytes{ 0 };
        /// @brief 업로드 스테이징 블록 크기(바이트)입니다(`EngineConfig::_rhi._uploadStagingMegabytes`). 블록을 나눠 쓰는 백엔드(Vulkan)는 0 이면 초기화를 거절한다. 리플렉션하지 않는다.
        uint64 _uploadStagingBytes{ 0 };
        /// @brief 쓰지 않은 업로드 스테이징 블록을 놓기까지의 프레임 수입니다(`EngineConfig::_rhi._uploadStagingIdleFrames`). 블록을 나눠 쓰는 백엔드(Vulkan)는 0 이면 초기화를 거절한다.
        uint32 _uploadStagingIdleFrames{ 0 };
        /// @brief 파이프라인 캐시 저장 주기(초)입니다(`EngineConfig::_rhi._pipelineCacheSaveIntervalSeconds`). 캐시를 디스크에 두는 백엔드(Vulkan · DX12)는 0 이하면 초기화를 거절한다. 리플렉션하지 않는다.
        float64 _pipelineCacheSaveIntervalSeconds{ 0.0 };
        uint8   _bSoftwareAdapter    : 1; ///< 소프트웨어 래스터라이저로 띄운다(DX12, DX11 WARP, Vulkan CPU 디바이스). CI 러너와 같은 조건을 재현하는 진단 스위치다.
        uint8   _bPreferMinimumPower : 1; ///< 하드웨어 어댑터를 저전력 순서로 고른다(하이브리드 노트북의 내장 GPU — 언리얼 -preferIntel 자리). 기본은 고성능 순서(제조사와 무관)
        /// @brief 그래픽 API 검증 층(D3D 디버그 레이어 · Vulkan 검증 레이어)을 켭니다(`resolveRHIDiagnosticSwitch` — EngineConfig `_rhi._validationLayer`, `-rhi-validation`).
        ///        배포가 아닌 구성(`SW_DO_CHECKS`)에서만 백엔드가 듣는다. 리플렉션하지 않는다.
        uint8 _bValidationLayer : 1;
        /// @brief D3D12 DRED(자동 브레드크럼 · 페이지 폴트)를 켭니다(`resolveRHIDiagnosticSwitch` — EngineConfig `_rhi._dred`, `-rhi-dred`). 배포 구성에서도 듣는다. 리플렉션하지 않는다.
        uint8 _bDRED : 1;
        /// @brief GPU 기반 검증을 켭니다(`EngineConfig::_rhi._bGPUBasedValidation`, 디버그 레이어가 켜진 실행만). 리플렉션하지 않는다.
        uint8 _bGPUBasedValidation : 1;
        /// @brief Vulkan 동기화 검증을 켭니다(`EngineConfig::_rhi._bSynchronizationValidation`, 검증 레이어가 켜진 실행만). 리플렉션하지 않는다.
        uint8 _bSynchronizationValidation : 1;

        RHIDeviceDesc() noexcept
            : _bSoftwareAdapter{ SW_FALSE }
            , _bPreferMinimumPower{ SW_FALSE }
            , _bValidationLayer{ SW_FALSE }
            , _bDRED{ SW_FALSE }
            , _bGPUBasedValidation{ SW_FALSE }
            , _bSynchronizationValidation{ SW_FALSE }
        {
        }
    };
} // namespace sw
