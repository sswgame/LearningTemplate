/**
 * @file DevCommandRegistry.h
 * @brief 개발 명령(치트 · 디버그 명령) 등록부와 `SW_DEV_COMMAND` 입니다. **Shipping 에서는 통째로 빠집니다.**
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

/** @brief 개발 명령이 컴파일되는 구성이면 1 입니다(Dev). Shipping 은 0 이고 등록부 · 콘솔 · 명령 본문이 모두 빠집니다. */
#if defined( SW_SHIPPING )
    #define SW_DEV_COMMANDS_ENABLED 0
#else
    #define SW_DEV_COMMANDS_ENABLED 1
#endif

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    /**
     * @brief 개발 명령 하나의 본문입니다. @p listArgument 는 명령 이름 뒤의 낱말(따옴표로 묶은 것은 한 낱말)입니다.
     * @return 인자가 잘못됐으면 false — 콘솔이 사용법(`_pUsage`)을 함께 알립니다. 답은 @p outReply 에 적습니다.
     */
    using DevCommandFunc = bool ( * )( const vector<string>& listArgument, string& outReply );

    /** @brief 개발 명령 하나의 등록 줄입니다. 문자열은 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct DevCommandRegistration
    {
        const utf8*    _pName;  ///< 콘솔에 치는 이름(점으로 묶는다: `debugdraw.category`). 대소문자를 가리지 않는다
        const utf8*    _pUsage; ///< 사용법 한 줄(`teleport <object> <x> <y> <z>`)
        const utf8*    _pHelp;  ///< 설명 한 줄
        DevCommandFunc _pFunc;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DevCommandRegistry
     * @brief 개발 명령의 등록부입니다(Engine 하나 — 게임 · 키트 · 에디터 모듈이 등록하고, 모듈을 내리면 그 명령이 빠집니다).
     * @details 명령은 자기 .cpp 에 `SW_DEV_COMMAND` 한 줄로 등록합니다. 정적 등록자가 이미지를 올릴 때 등록하고 내릴 때(핫 리로드) 뺍니다 —
     *          등록부는 Engine 에 있어 모듈이 바뀌어도 살아 있습니다. 같은 이름의 둘째 등록은 경고와 함께 거절합니다. 게임 스레드에서만 씁니다.
     */
    class SW_API DevCommandRegistry
    {
    public:
        /** @brief 프로세스에 하나인 등록부입니다(Engine 이미지의 함수 정적). */
        static DevCommandRegistry& get();
        /**
         * @brief 이 등록부가 이미지에 들어 있다는 표식 글입니다. Shipping 실행 파일에는 이 글이 없어야 합니다
         *        (`DevCommandShippingTest` 가 바이너리를 훑어 확인합니다).
         */
        static const utf8* getImageMarker();

        /** @brief 명령을 올립니다. 이름이 비었거나 이미 있으면 false 입니다. */
        bool registerCommand( const DevCommandRegistration* pRegistration );
        /** @brief 명령을 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterCommand( const DevCommandRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 명령을 찾습니다. */
        const DevCommandRegistration* findCommand( string_view name ) const;
        /** @brief @p prefix 로 시작하는(대소문자 무시) 명령 이름을 사전순으로 채웁니다. */
        void collectNames( string_view prefix, vector<string>& outListName ) const;
        /** @brief 등록된 명령 수입니다. */
        uint32 getCount() const { return static_cast<uint32>( _listRegistration.size() ); }

    private:
        vector<const DevCommandRegistration*> _listRegistration;
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
        static const ::sw::DevCommandRegistrar    sw_devCommandRegistrar_##varName { &sw_devCommandRegistration_##varName }

#else

    #define SW_DEV_COMMAND( varName, pName, pUsage, pHelp, pFunc ) static_assert( true, "SW_DEV_COMMAND is compiled out of Shipping" )

#endif
