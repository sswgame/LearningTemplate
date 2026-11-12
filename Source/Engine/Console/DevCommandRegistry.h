/**
 * @file DevCommandRegistry.h
 * @brief 개발 명령(치트, 디버그 명령) 레지스트리와 `SW_DEV_COMMAND` 를 정의합니다.
 * @details 선언과 인자 도우미(`readDiskPathArgument` …)는 모든 구성에 있다 — 기능 파일의 명령 본문이 모든 구성에서 컴파일되게. 배포 구성은 `SW_DEV_COMMAND` 가
 *          등록하지 않아 레지스트리가 비어 있고, 본문은 부르는 곳이 없어 실행 파일에서 빠진다(`docs/agents/BuildConfigSwitch.md`).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Module/ModuleEntryRegistrar.h"

namespace sw
{
    /**
     * @brief 개발 명령 하나의 본문입니다. @p listArgument 는 명령 이름 뒤의 단어입니다(따옴표로 묶은 것은 한 단어입니다).
     * @return 인자가 잘못됐으면 false 이며, 콘솔이 사용법(`_pUsage`)을 함께 알립니다. 답은 @p outReply 에 적습니다.
     */
    using DevCommandFunc = bool ( * )( const vector<string>& listArgument, string& outReply );

    /** @brief 개발 명령 하나의 등록 줄입니다. 문자열은 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct DevCommandRegistration
    {
        const utf8*    _pName;  ///< 콘솔에 치는 이름입니다(점으로 묶습니다. 예: `debugdraw.category`). 대소문자를 가리지 않습니다.
        const utf8*    _pUsage; ///< 사용법 한 줄(`teleport <object> <x> <y> <z>`)
        const utf8*    _pHelp;  ///< 설명 한 줄입니다.
        DevCommandFunc _pFunc;
    };

    /**
     * @brief 개발 명령의 선택 불리언 인자(`hud on`, `cheat.god off` 등)를 읽습니다. 철자 표는 `StringUtil::tryParseBool` 하나입니다.
     * @details 명령마다 `== "on"` 을 손으로 비교하면 철자(`1`, `true`, 대소문자)가 명령마다 갈라졌다. 인자 읽기는 이 함수로만 한다.
     * @param index    읽을 인자의 자리입니다.
     * @param outValue 인자가 없으면 건드리지 않으므로(호출자의 뒤집기 기본값이 남는다) 미리 기본값을 넣어 두세요. 읽지 못하면 그대로 둡니다.
     * @return 인자가 없거나(기본값을 쓴다) 불리언으로 읽었으면 true, 있는데 읽지 못하면 false(사용법으로 거절)입니다.
     */
    [[nodiscard]] inline bool readOptionalBoolArgument( const vector<string>& listArgument, size_t index, bool& outValue )
    {
        return index >= listArgument.size() || StringUtil::tryParseBool( listArgument[index], outValue );
    }
} // namespace sw

namespace sw
{
    /**
     * @class DevCommandRegistry
     * @brief 개발 명령의 레지스트리입니다(엔진 서비스 범위가 소유한다). 엔진 · 게임 · 키트 · 에디터 모듈이 등록하고 모듈을 내리기 전에 뺀다.
     * @details 명령은 자기 .cpp 에 `SW_DEV_COMMAND` 한 줄로 등록한다. 등록자는 링크 때 목록에만 달리고 모듈을 올리는 길이 붙인다(`DevCommandRegistrar`) — 정적 초기화 때는 서비스가 아직 없다.
     *          같은 이름의 둘째 등록은 경고와 함께 거절한다.
     *          등록 · 해제는 이미지 로더 스레드(정적 초기화)에서, 조회는 콘솔(게임 스레드)에서 오므로 등록 목록은 잠금 하나로 지킨다.
     *          명령 본문은 잠금 밖에서 부른다(찾은 등록 줄은 그 모듈이 내려가기 전까지 산다). 프레임마다 도는 경로가 아니다.
     */
    class SW_API DevCommandRegistry final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "DevCommandRegistry";

        DevCommandRegistry();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 쓸 자리 · 읽을 디스크 파일 인자를 읽습니다 - 절대 경로(시나리오의 `{out}/…`)만 받는다. 작업 폴더 상대 경로는 받지 않는다(R5 - 작업 폴더를 바꾸면 다른 곳을 썼다).
         * @return 절대 경로면 구분자를 `/` 로 맞춰 @p outAbsolutePath 에 담고 true, 아니면 이유를 @p outReply 에 담고 false 입니다. 명령이 모두 같은 입구로 읽는다.
         */
        [[nodiscard]] static bool readDiskPathArgument( string_view argument, string& outAbsolutePath, string& outReply );
        /** @brief 개수 인자(프레임 수 · 초)를 읽습니다. 숫자만으로 된 1000000 이하의 값이면 true 입니다(부호 · 공백 · 소수는 받지 않는다). */
        [[nodiscard]] static bool readCountArgument( string_view argument, uint32& outValue );

        /** @brief 명령을 올립니다. 이름이 비었거나 이미 있으면 false 입니다. */
        [[nodiscard]] bool registerEntry( const DevCommandRegistration* pRegistration );
        /** @brief 명령을 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterEntry( const DevCommandRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 명령을 찾습니다. */
        const DevCommandRegistration* findCommand( string_view name ) const;
        /** @brief @p prefix 로 시작하는(대소문자 무시) 명령 이름을 사전순으로 채웁니다. */
        void collectNames( string_view prefix, vector<string>& outListName ) const;
        /** @brief 등록된 명령 수입니다. */
        uint32 getCount() const;

    private:
        mutable mutex                                                _mutex;                  ///< 등록 목록을 지킨다(명령 실행은 잠금 밖)
        RegistrationList<const DevCommandRegistration> _registration SW_GUARDED_BY( _mutex ); ///< 이름은 대소문자를 무시하고, 이름 없는 등록은 거절한다

        SW_SERVICE_SLOT( DevCommandRegistry, Required );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_DEV_COMMAND` 가 두는 정적 등록자입니다. */
    using DevCommandRegistrar = ModuleEntryRegistrar<DevCommandRegistry, const DevCommandRegistration*>;
} // namespace sw

#if !defined( SW_SHIPPING )

/**
 * @brief 개발 명령을 기능의 `.cpp` 에서 등록합니다(언리얼 `FAutoConsoleCommand` 처럼 기능 옆에 둔다).
 *        배포 구성은 등록하지 않고 본문 함수를 `[[maybe_unused]]` 상수로만 붙잡는다 — 본문은 모든 구성에서 컴파일되고(썩지 않는다) 부르는 곳이 없어 배포 실행 파일에서 빠진다.
 *        파일 이름으로 배포에서 빼지 않는다(`docs/agents/BuildConfigSwitch.md`).
 * @param varName 파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName   콘솔에 치는 이름(리터럴)
 * @param pUsage  사용법 한 줄
 * @param pHelp   설명 한 줄
 * @param pFunc   `bool( const vector<string>&, string& )` 본문
 */
    #define SW_DEV_COMMAND( varName, pName, pUsage, pHelp, pFunc )                                                    \
        static const ::sw::DevCommandRegistration sw_devCommandRegistration_##varName{ pName, pUsage, pHelp, pFunc }; \
        static ::sw::DevCommandRegistrar          sw_devCommandRegistrar_##varName                                    \
        {                                                                                                             \
            &sw_devCommandRegistration_##varName                                                                      \
        }

#else

    #define SW_DEV_COMMAND( varName, pName, pUsage, pHelp, pFunc ) [[maybe_unused]] static constexpr auto sw_devCommandShippingUnused_##varName = pFunc

#endif
