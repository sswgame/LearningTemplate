/**
 * @file IModuleCompiler.h
 * @brief 인-에디터 백그라운드 컴파일러 서비스 인터페이스
 *
 * @note 이것은 **C-ABI 가 아니라 C++ 가상 함수 테이블**이다. 서비스 로케이터로 건네지므로
 *       호스트와 모듈이 같은 툴체인·같은 플래그로 빌드된다는 전제에 의존한다. 진짜 C 경계는
 *       `ABI/` 쪽의 함수 포인터 테이블뿐이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @brief 컴파일러 빌드 상태
     */
    enum class BuildState : uint8
    {
        Idle = 0,
        Compiling,
        Success,
        Failed
    };

    /**
     * @class IModuleCompiler
     * @brief 백그라운드 C++ 모듈 컴파일러 제어 인터페이스
     */
    class IModuleCompiler
    {
    public:
        virtual ~IModuleCompiler() = default;

        /**
         * @brief 지정된 타겟 모듈(예: "SWGame", "EditorModule")을 비동기로 컴파일합니다.
         * @param targetName 빌드할 CMake 타겟 이름
         * @return 이미 컴파일 중이면 false, 성공적으로 작업을 시작하면 true
         */
        virtual bool compileModule( string_view targetName ) = 0;

        /**
         * @brief 전체 모듈을 비동기로 컴파일합니다.
         * @return 이미 컴파일 중이면 false, 성공적으로 작업을 시작하면 true
         */
        virtual bool compileAll() = 0;

        /** @brief 진행 중인 빌드 프로세스를 취소합니다. */
        virtual void cancel() = 0;

        /** @brief 현재 빌드 상태를 반환합니다. */
        virtual BuildState getBuildState() const = 0;
        /** @brief 현재 컴파일 작업이 진행 중인지 여부를 반환합니다. */
        virtual bool isCompiling() const = 0;
        /** @brief 현재 진행 중인 빌드의 경과 시간(초)을 반환합니다. */
        virtual float32 getElapsedTimeSec() const = 0;
        /** @brief 마지막 빌드에 소요된 시간(초)을 반환합니다. */
        virtual float32 getLastDurationSec() const = 0;
        /** @brief 마지막 빌드 프로세스의 종료 코드를 반환합니다. (0 = 성공) */
        virtual int32 getLastExitCode() const = 0;
        /**
         * @brief 현재 또는 마지막으로 컴파일된 타겟 이름을 반환합니다.
         * @note **값으로 돌려주는 것이 의도다.** 이름은 빌드 워커 스레드가 갱신하므로 구현이
         *       잠금 아래에서 복사해 넘긴다. `const utf8*` 로 바꾸면 호출자가 잠금 밖에서
         *       읽게 되어 데이터 레이스가 된다.
         */
        virtual string getTargetName() const = 0;
    };
} // namespace sw
