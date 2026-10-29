/**
 * @file DevCommandRegistry.h
 * @brief 개발 명령(치트, 디버그 명령) 레지스트리와 `SW_DEV_COMMAND` 를 정의합니다. **Shipping 에서는 통째로 빠집니다.**
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"

#if SW_DEV_COMMANDS_ENABLED

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
     * @brief 개발 명령의 레지스트리입니다(Core 이미지에 하나 있으며, 엔진, 게임, 키트, 에디터 모듈이 등록하고 모듈을 언로드하면 그 명령이 빠집니다).
     * @details 명령은 자기 .cpp 에 `SW_DEV_COMMAND` 한 줄로 등록한다. 정적 등록자가 이미지를 로드할 때 등록하고 언로드할 때(핫 리로드) 뺀다.
     *          레지스트리는 Core 에 있어 모듈이 바뀌어도 살아 있다. 같은 이름의 둘째 등록은 경고와 함께 거절한다.
     *          등록 · 해제는 이미지 로더 스레드(정적 초기화)에서, 조회는 콘솔(게임 스레드)에서 오므로 등록 목록은 잠금 하나로 지킨다.
     *          명령 본문은 잠금 밖에서 부른다(찾은 등록 줄은 그 모듈이 내려가기 전까지 산다). 프레임마다 도는 경로가 아니다.
     */
    class SW_API DevCommandRegistry
    {
    public:
        /** @brief 프로세스에 하나인 레지스트리입니다(Core 이미지의 함수 정적). */
        static DevCommandRegistry& get();

        DevCommandRegistry();
        /**
         * @brief 이 레지스트리가 이미지에 들어 있다는 표식 글입니다. Shipping 실행 파일에는 이 글이 없어야 합니다
         *        (`DevCommandShippingTest` 가 바이너리를 훑어 확인합니다).
         */
        static const utf8* getImageMarker();

        /** @brief 명령 줄을 낱말로 나눕니다. 큰따옴표로 묶은 것은 공백이 있어도 한 낱말입니다(콘솔 · 시나리오 `<Command>` 단계가 같은 규칙을 쓴다). */
        static void tokenize( string_view line, vector<string>& outListToken );

        /** @brief 명령을 올립니다. 이름이 비었거나 이미 있으면 false 입니다. */
        [[nodiscard]] bool registerCommand( const DevCommandRegistration* pRegistration );
        /** @brief 명령을 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterCommand( const DevCommandRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 명령을 찾습니다. */
        const DevCommandRegistration* findCommand( string_view name ) const;
        /** @brief @p prefix 로 시작하는(대소문자 무시) 명령 이름을 사전순으로 채웁니다. */
        void collectNames( string_view prefix, vector<string>& outListName ) const;
        /** @brief 등록된 명령 수입니다. */
        uint32 getCount() const;

    private:
        mutable mutex                                                _mutex;                  ///< 등록 목록을 지킨다(명령 실행은 잠금 밖)
        RegistrationList<const DevCommandRegistration> _registration SW_GUARDED_BY( _mutex ); ///< 이름은 대소문자를 무시하고, 이름 없는 등록은 거절한다
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_DEV_COMMAND` 가 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_API DevCommandRegistrar
    {
        explicit DevCommandRegistrar( const DevCommandRegistration* pRegistration );
        ~DevCommandRegistrar();
        DevCommandRegistrar( const DevCommandRegistrar& )            = delete;
        DevCommandRegistrar& operator=( const DevCommandRegistrar& ) = delete;

        const DevCommandRegistration* _pRegistration;
        bool                          _bRegistered;
    };
} // namespace sw

/**
 * @brief 개발 명령을 그 명령의 .cpp 에서 등록합니다. Shipping 에서는 아무것도 남기지 않습니다 — 본문 함수도 `#if SW_DEV_COMMANDS_ENABLED`
 *        안에 두십시오(밖에 두면 Shipping 에서 쓰이지 않는 함수가 됩니다).
 * @param varName 파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName   콘솔에 치는 이름(리터럴)
 * @param pUsage  사용법 한 줄
 * @param pHelp   설명 한 줄
 * @param pFunc   `bool( const vector<string>&, string& )` 본문
 */
    #define SW_DEV_COMMAND( varName, pName, pUsage, pHelp, pFunc )                                                    \
        static const ::sw::DevCommandRegistration sw_devCommandRegistration_##varName{ pName, pUsage, pHelp, pFunc }; \
        static const ::sw::DevCommandRegistrar    sw_devCommandRegistrar_##varName                                    \
        {                                                                                                             \
            &sw_devCommandRegistration_##varName                                                                      \
        }

#else

    #define SW_DEV_COMMAND( varName, pName, pUsage, pHelp, pFunc ) static_assert( true, "SW_DEV_COMMAND is compiled out of Shipping" )

#endif
