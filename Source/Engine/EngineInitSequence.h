/**
 * @file EngineInitSequence.h
 * @brief 자기 클래스가 없는 엔진 기동 단계의 선언(`EngineFunctionStep`)입니다. 순서 계산 · 실행은 `SubsystemCollection` 이 합니다.
 * @details 의존 칸은 다른 선언의 이름 상수를 가리키는 배열이라 철자가 틀리면 컴파일 오류다. 선언은 의존이 앞에 오도록 적는다(constexpr 멤버는 앞의 것만 볼 수 있다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Common/FunctionSubsystem.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Physics/PhysicsSystem.h"

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
        /** @brief 코덱 레지스트리 + Core 슬롯 + (엔진 루프만) 외부 코덱. 맨 먼저 서므로 맨 나중에 소멸한다 — 팩을 내리는 Resource 해제까지 코덱이 살아 있다. */
        static constexpr FunctionSubsystemDesc kCompression{ "Compression", {}, SubsystemHostMask::All, MemoryTag::EngineMisc };
        /** @brief 이 프로세스에 올라온 타입 등록기를 모으고 명령줄의 enum 이름을 적용한다. 설정 · 에셋 · 씬 객체가 이 뒤에 서서 먼저 소멸한다. */
        static constexpr FunctionSubsystemDesc kReflection{ "Reflection", {}, SubsystemHostMask::All, MemoryTag::Reflection };
        /** @brief 태스크 매니저. 소멸자가 태스크를 기다리는 객체(에셋 스트리밍 큐)는 이것에 의존하는 단계(Scene)가 먼저 놓는다. */
        static constexpr FunctionSubsystemDesc kTask{ "Task", {}, SubsystemHostMask::All, MemoryTag::Task };

        static constexpr const utf8* kArrConfigDependency[] = { kReflection._pName };
        /** @brief 엔진 · 게임 설정(EngineConfig · GameConfig)과 설정 표. 설정 역직렬화가 TypeInfo 를 쓴다. */
        static constexpr FunctionSubsystemDesc kConfig{ "Config", kArrConfigDependency, SubsystemHostMask::All, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrResourceDependency[] = { kCompression._pName, kConfig._pName };
        /**
         * @brief 에셋 매니저 + 콘텐츠 마운트. 팩 루트는 GameConfig, 팩 스트림은 코덱이다.
         * @details 해제(`AssetManager::shutdown`)가 에셋 캐시를 비운다 — 에셋을 드는 단계가 모두 이 뒤에 서므로, 역순 해제에서 그들의 소멸자가 에셋을 놓은 뒤다.
         */
        static constexpr FunctionSubsystemDesc kResource{ "Resource", kArrResourceDependency, SubsystemHostMask::All, MemoryTag::Asset };

        static constexpr const utf8* kArrEngineDefaultAssetsDependency[] = { kReflection._pName, kResource._pName };
        /** @brief 엔진 데이터(EngineDefaultAssets)와 엔진 문화권 표 · 문자열. 하네스는 읽지 않고 소유만 한다. */
        static constexpr FunctionSubsystemDesc kEngineDefaultAssets{ "EngineDefaultAssets", kArrEngineDefaultAssetsDependency, SubsystemHostMask::All, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrModuleImagesDependency[] = { kCompression._pName, kReflection._pName, kResource._pName, kTask._pName };
        /** @brief 종료 쪽 자리 — 씬은 사라지고 서비스는 살아 있는 구간에서 모듈 DLL 을 내리는 호스트 훅. 타입 · 코덱 · 캐시를 떼고 태스크를 기다린다. */
        static constexpr FunctionSubsystemDesc kModuleImages{ "ModuleImages", kArrModuleImagesDependency, SubsystemHostMask::All, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrAudioDependency[] = { kModuleImages._pName, kTask._pName };
        /** @brief 오디오 서비스. 음성 스트리밍이 태스크를 쓰고, 모듈 콜백을 든다 — 서버는 장치 없는 NullAudioBackend. */
        static constexpr FunctionSubsystemDesc kAudio{ "Audio", kArrAudioDependency, SubsystemHostMask::All, MemoryTag::Audio };

        static constexpr const utf8* kArrFileIODependency[] = { kModuleImages._pName, kTask._pName };
        /** @brief 비동기 파일 IO. 완료 콜백을 태스크로 싣고, 모듈을 내리기 전에 걸린 읽기를 다 끝낸다. */
        static constexpr FunctionSubsystemDesc kFileIO{ "FileIO", kArrFileIODependency, SubsystemHostMask::All, MemoryTag::Asset };

        static constexpr const utf8* kArrInputDependency[] = { kModuleImages._pName };
        /** @brief 입력 매니저. 모듈이 등록한 장치 · 콜백을 든다 — 서버는 이벤트가 오지 않는 장치 목록이다. */
        static constexpr FunctionSubsystemDesc kInput{ "Input", kArrInputDependency, SubsystemHostMask::All, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrModuleTypesDependency[] = { kConfig._pName, kModuleImages._pName, kReflection._pName };
        /** @brief 호스트가 타입 공급자(GF · 킷 · 게임 모듈)를 올려 등록을 끝낸다 — 씬을 읽는 일은 이 뒤다. */
        static constexpr FunctionSubsystemDesc kModuleTypes{ "ModuleTypes", kArrModuleTypesDependency, SubsystemHostMask::All, MemoryTag::Reflection };

        static constexpr const utf8* kArrSceneDependency[] = { kAudio._pName, kFileIO._pName, kInput._pName, kModuleImages._pName, PhysicsSystem::kSubsystemName, kResource._pName, kTask._pName };
        /**
         * @brief 씬 매니저 · 에셋 스트리밍 큐 · 되돌리기 기록. 컴포넌트가 입력 · 오디오 · 모듈 코드 · 에셋 · 물리 씬을 든다(스트리밍은 비동기 IO).
         * @details 스트리밍 큐의 소멸자는 태스크를 기다린다 — Task 에 의존하는 이 단계가 Task 보다 먼저 놓는다.
         */
        static constexpr FunctionSubsystemDesc kScene{ "Scene", kArrSceneDependency, SubsystemHostMask::All, MemoryTag::Scene };

        static constexpr const utf8* kArrHeadlessDependency[] = { kEngineDefaultAssets._pName, kModuleTypes._pName, kScene._pName, ShaderCache::kSubsystemName };
        /**
         * @brief 헤드리스 작업 — 셰이더 쿠킹 · 씬 쿠킹(모든 컴포넌트 타입이 있어야 쿠킹한다) · 임포트 준비. 하면 뒤 단계(창 · RHI · 렌더러)를 건너뛴다.
         *        서버는 씬 쿠킹만 하고 셰이더 쿠킹 · 임포트는 거절한다.
         */
        static constexpr FunctionSubsystemDesc kHeadless{ "Headless", kArrHeadlessDependency, SubsystemHostMask::All, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrUserSettingsDependency[] = { kAudio._pName, kHeadless._pName, kInput._pName, kResource._pName };
        /** @brief 플레이어 옵션 — 스키마 · 사용자 파일을 읽어 대상(오디오 · 입력 · 전역 변수)에 넣는다. 화면 값은 창을 만드는 RHI 가 읽는다. */
        static constexpr FunctionSubsystemDesc kUserSettings{ "UserSettings", kArrUserSettingsDependency, EngineSubsystemHost::kClient, MemoryTag::EngineMisc };

        static constexpr const utf8* kArrRHIDependency[] = { kConfig._pName, kHeadless._pName, kUserSettings._pName };
        /**
         * @brief 백엔드 선택 · 창 · 디바이스. 설정(기본 백엔드 · 창 크기)과 플레이어 화면 설정을 읽고, 헤드리스 작업이면 서지 않는다.
         * @details 백엔드 교체의 기준점이다 — `EngineLoop::applyPendingBackendChange` 가 이것에 기대는 단계만 내렸다가 다시 세운다(`shutdownDependentsOf`).
         */
        static constexpr FunctionSubsystemDesc kRHI{ "RHI", kArrRHIDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrFrameRendererDependency[] = { kRHI._pName, kTask._pName };
        /** @brief 렌더러와 GT 쪽 패킷 빌더(씬 스냅샷 · 업로드 큐). 백엔드 교체로 다시 선다. */
        static constexpr FunctionSubsystemDesc kFrameRenderer{ "FrameRenderer", kArrFrameRendererDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrRenderThreadDependency[] = { kFrameRenderer._pName };
        /** @brief 렌더 스레드. 다시 설 때 같은 객체에 붙는다(훅 · 포인터 유지). */
        static constexpr FunctionSubsystemDesc kRenderThread{ "RenderThread", kArrRenderThreadDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        static constexpr const utf8* kArrLiveShaderDependency[] = { kFrameRenderer._pName, kRenderThread._pName };
        /** @brief 셰이더 라이브 리로드(Debug). 재컴파일이 렌더 스레드를 세우고 렌더러에 알린다. */
        static constexpr FunctionSubsystemDesc kLiveShader{ "LiveShader", kArrLiveShaderDependency, EngineSubsystemHost::kClient, MemoryTag::Shader };

        static constexpr const utf8* kArrSceneRHIDependency[] = { kRHI._pName, kScene._pName };
        /** @brief 씬에 디바이스를 건다 — 렌더러 쪽 단계 가운데 종료가 가장 먼저다. */
        static constexpr FunctionSubsystemDesc kSceneRHI{ "SceneRHI", kArrSceneRHIDependency, EngineSubsystemHost::kClient, MemoryTag::RenderCPU };

        /** @brief 위 선언 모두입니다(테스트가 엔진 목록을 본문 없이 짓는다). */
        static constexpr const FunctionSubsystemDesc* kArrAll[] = { &kCompression, &kReflection, &kTask, &kConfig, &kResource, &kEngineDefaultAssets, &kModuleImages, &kAudio, &kFileIO, &kInput,
                                                                    &kModuleTypes, &kScene, &kHeadless, &kUserSettings, &kRHI, &kFrameRenderer, &kRenderThread, &kLiveShader, &kSceneRHI };
    };
} // namespace sw
