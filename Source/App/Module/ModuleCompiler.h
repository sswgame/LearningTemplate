/**
 * @file ModuleCompiler.h
 * @brief 인-에디터 백그라운드 비동기 C++ 컴파일러 구현체 (App 계층)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/CpuTimer.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

namespace sw
{
    class LiveReloadManager;
    class Process;

    /**
     * @class ModuleCompiler
     * @brief Process 유틸리티를 활용해 CMake 백그라운드 빌드를 구동하고 LiveReloadManager에 핫스왑을 요청하는 IModuleCompiler 구현체
     */
    class ModuleCompiler : public IModuleCompiler
    {
    public:
        /** @brief 컴파일러를 생성합니다. */
        ModuleCompiler( LiveReloadManager* pLiveReloadManager = nullptr );
        /** @brief 컴파일러를 파괴하고 진행 중인 빌드 스레드를 정리합니다. */
        virtual ~ModuleCompiler() override;

        ModuleCompiler( const ModuleCompiler& )            = delete;
        ModuleCompiler& operator=( const ModuleCompiler& ) = delete;

        /** @brief 라이브 리로드 매니저를 연결하여 초기화합니다. */
        void initialize( LiveReloadManager* pLiveReloadManager );
        /** @brief 진행 중인 빌드를 중단하고 리소스를 해제합니다. */
        void shutdown();

        /**
         * @brief 지정된 타겟 모듈(예: "SWGame", "EditorModule")을 비동기로 컴파일합니다.
         * @param targetName 빌드할 CMake 타겟 이름
         * @return 이미 컴파일 중이면 false, 성공적으로 작업을 시작하면 true
         */
        virtual bool compileModule( string_view targetName ) override;

        /**
         * @brief 전체 모듈을 비동기로 컴파일합니다.
         * @return 이미 컴파일 중이면 false, 성공적으로 작업을 시작하면 true
         */
        virtual bool compileAll() override;

        /** @brief 진행 중인 빌드 프로세스를 취소합니다. */
        virtual void cancel() override;

        /** @brief 현재 빌드 상태를 반환합니다. */
        virtual BuildState getBuildState() const override { return _buildState.load( std::memory_order_relaxed ); }
        /** @brief 현재 컴파일 작업이 진행 중인지 여부를 반환합니다. */
        virtual bool isCompiling() const override { return _bIsCompiling.load( std::memory_order_relaxed ); }
        /** @brief 현재 진행 중인 빌드의 경과 시간(초)을 반환합니다. */
        virtual float32 getElapsedTimeSec() const override;
        /** @brief 마지막 빌드에 소요된 시간(초)을 반환합니다. */
        virtual float32 getLastDurationSec() const override { return _lastDurationSec.load( std::memory_order_relaxed ); }
        /** @brief 마지막 빌드 프로세스의 종료 코드를 반환합니다. (0 = 성공) */
        virtual int32 getLastExitCode() const override { return _lastExitCode.load( std::memory_order_relaxed ); }
        /** @brief 현재 또는 마지막으로 컴파일된 타겟 이름을 반환합니다. */
        virtual string getTargetName() const override;

    private:
        void   runBuildThread( string targetName );
        string findBuildDirectory() const;

    private:
        LiveReloadManager*  _pLiveReloadManager;
        unique_ptr<Process> _pCurrentProcess;
        std::thread         _workerThread;
        CpuTimer            _buildTimer;
        string              _targetName;
        mutable mutex       _mutex;
        atomic<BuildState>  _buildState;
        atomic<int32>       _lastExitCode;
        atomic<float32>     _lastDurationSec;
        atomic<bool>        _bIsCompiling;
        atomic<bool>        _bCancelRequested;
    };
} // namespace sw
