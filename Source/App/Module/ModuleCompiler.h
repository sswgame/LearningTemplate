/**
 * @file ModuleCompiler.h
 * @brief 에디터 안에서 C++ 모듈을 백그라운드로 비동기 컴파일하는 구현입니다(App 계층).
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
     * @brief Process 유틸리티로 CMake 빌드를 백그라운드에서 돌리고, LiveReloadManager 에 핫 스왑을 요청하는 IModuleCompiler 구현입니다.
     */
    class ModuleCompiler : public IModuleCompiler
    {
    public:
        /** @brief 컴파일러를 만듭니다. */
        ModuleCompiler( LiveReloadManager* pLiveReloadManager = nullptr );
        /** @brief 컴파일러를 파괴하고 진행 중인 빌드 스레드를 정리합니다. */
        virtual ~ModuleCompiler() override;

        ModuleCompiler( const ModuleCompiler& )            = delete;
        ModuleCompiler& operator=( const ModuleCompiler& ) = delete;

        /** @brief 진행 중인 빌드를 멈추고 자원을 해제합니다. */
        void shutdown();

        /**
         * @brief 지정한 타깃 모듈(예: "SWGame", "EditorModule")을 비동기로 컴파일합니다.
         * @param targetName 빌드할 CMake 타깃 이름
         * @return 이미 컴파일 중이면 false, 작업을 시작했으면 true
         */
        virtual bool compileModule( string_view targetName ) override;

        /**
         * @brief 모든 모듈을 비동기로 컴파일합니다.
         * @return 이미 컴파일 중이면 false, 작업을 시작했으면 true
         */
        virtual bool compileAll() override;

        /** @brief 진행 중인 빌드 프로세스를 취소합니다. */
        virtual void cancel() override;

        /** @brief 현재 빌드 상태를 반환합니다. */
        virtual BuildState getBuildState() const override { return _buildState.load( std::memory_order_relaxed ); }
        /** @brief 지금 컴파일 중인지 반환합니다. */
        virtual bool isCompiling() const override { return _bIsCompiling.load( std::memory_order_relaxed ); }
        /** @brief 현재 진행 중인 빌드의 경과 시간(초)을 반환합니다. */
        virtual float32 getElapsedTimeSec() const override;
        /** @brief 마지막 빌드에 걸린 시간(초)을 반환합니다. */
        virtual float32 getLastDurationSec() const override { return _lastDurationSec.load( std::memory_order_relaxed ); }
        /** @brief 마지막 빌드 프로세스의 종료 코드를 반환합니다(0 = 성공). */
        virtual int32 getLastExitCode() const override { return _lastExitCode.load( std::memory_order_relaxed ); }

        /**
         * @brief 마지막 빌드가 "실행 중인 바이너리를 교체할 수 없어서" 실패했는지 반환합니다.
         * @details 엔진 자체를 다시 링크해야 하는 변경이면 핫 리로드로는 해결되지 않습니다. 이 경우 빌드 실패는 코드 오류가 아니라
         *          환경 제약이므로, 호출하는 쪽이 구분해서 다룰 수 있게 합니다.
         */
        bool wasBlockedByLoadedBinary() const { return _bBlockedByLoadedBinary.load( std::memory_order_relaxed ) != 0; }
        /** @brief 현재 또는 마지막으로 컴파일한 타깃 이름을 반환합니다. */
        virtual string getTargetName() const override;

    private:
        void   runBuildThread( const string& targetName );
        string findBuildDirectory() const;

    private:
        /// @brief 빌드가 끝나면 리로드를 걸 대상입니다. Shipping 에는 핫 리로드가 없어 아무도 읽지 않지만,
        ///        소유자(ModuleHost)가 빌드 구성에 따라 달라지지 않도록 필드 자체는 그대로 둡니다.
        [[maybe_unused]] LiveReloadManager* _pLiveReloadManager;
        unique_ptr<Process>                 _pCurrentProcess;
        std::thread                         _workerThread;
        CpuTimer                            _buildTimer;
        string                              _targetName;
        mutable mutex                       _mutex;
        atomic<BuildState>                  _buildState;
        /// @brief 실행 중인 바이너리를 다시 링크하려다 막혔는지 여부입니다(핫 리로드로는 해결할 수 없습니다).
        atomic<uint8>   _bBlockedByLoadedBinary;
        atomic<int32>   _lastExitCode;
        atomic<float32> _lastDurationSec;
        atomic<bool>    _bIsCompiling;
        atomic<bool>    _bCancelRequested;
    };
} // namespace sw
