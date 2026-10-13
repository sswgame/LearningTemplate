/**
 * @file EngineInitSequence.h
 * @brief 자기 클래스가 없는 엔진 기동 단계의 선언(`EngineFunctionStep`)입니다. 순서 계산 · 실행은 `SubsystemCollection` 이 합니다.
 * @details 의존 칸은 다른 선언의 이름 상수를 가리키는 배열이라 철자가 틀리면 컴파일 오류다. 선언은 의존이 앞에 오도록 적는다(constexpr 멤버는 앞의 것만 볼 수 있다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Common/FunctionSubsystem.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Module/ReflectionSubsystem.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    /**
     * @brief 자기 클래스가 없는 엔진 기동 단계의 선언(이름 · 의존 · 호스트 · 메모리 태그)입니다. 본문은 호스트가 `FunctionSubsystem` 의 델리게이트로 줍니다.
     * @details 호스트가 엮는 단계(모듈 훅 · 타입 공급자 로더 · 씬의 디바이스)와 Core 서비스(태스크 · 비동기 IO · 코덱 레지스트리)를 세우는 단계입니다.
     *          본문은 호스트마다 다를 수 있지만(엔진 루프 · 테스트 하네스) 선언은 여기 하나라 두 호스트가 같은 순서로 섭니다.
     *          클래스가 있는 단계(`ShaderCache` · `PhysicsSystem` …)는 그 클래스가 스스로 선언합니다.
     */
    struct EngineFunctionStep
    {
        static constexpr const utf8* kArrRHIDependency[] = { ConfigManager::kSubsystemName, HeadlessSubsystem::kSubsystemName, UserSettingsManager::kSubsystemName };
        /**
         * @brief 백엔드 선택 · 창 · 디바이스. 설정(기본 백엔드 · 창 크기)과 플레이어 화면 설정을 읽고, 헤드리스 작업이면 서지 않는다.
         * @details 백엔드 교체의 기준점이다 — `EngineLoop::applyPendingBackendChange` 가 이것에 기대는 단계만 내렸다가 다시 세운다(`shutdownDependentsOf`).
         */
        static constexpr FunctionSubsystemDesc kRHI{ "RHI", kArrRHIDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrFrameRendererDependency[] = { kRHI._pName, TaskManager::kSubsystemName };
        /** @brief 렌더러와 GT 쪽 패킷 빌더(씬 스냅샷 · 업로드 큐). 백엔드 교체로 다시 선다. */
        static constexpr FunctionSubsystemDesc kFrameRenderer{ "FrameRenderer", kArrFrameRendererDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrRenderThreadDependency[] = { kFrameRenderer._pName };
        /** @brief 렌더 스레드. 다시 설 때 같은 객체에 붙는다(훅 · 포인터 유지). */
        static constexpr FunctionSubsystemDesc kRenderThread{ "RenderThread", kArrRenderThreadDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrLiveShaderDependency[] = { kFrameRenderer._pName, kRenderThread._pName };
        /** @brief 셰이더 라이브 리로드(Debug). 재컴파일이 렌더 스레드를 세우고 렌더러에 알린다. */
        static constexpr FunctionSubsystemDesc kLiveShader{ "LiveShader", kArrLiveShaderDependency, EngineSubsystemHost::kClient, MemoryTag::Shader };

        static constexpr const utf8* kArrSceneRHIDependency[] = { kRHI._pName, SceneManager::kSubsystemName };
        /** @brief 씬에 디바이스를 건다 — 렌더러 쪽 단계 가운데 종료가 가장 먼저다. */
        static constexpr FunctionSubsystemDesc kSceneRHI{ "SceneRHI", kArrSceneRHIDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        /** @brief 위 선언 모두입니다(테스트가 엔진 목록을 본문 없이 짓는다). */
        static constexpr const FunctionSubsystemDesc* kArrAll[] = { &kRHI, &kFrameRenderer, &kRenderThread, &kLiveShader, &kSceneRHI };
    };
} // namespace sw
