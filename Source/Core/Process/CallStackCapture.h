/**
 * @file CallStackCapture.h
 * @brief 크로스 플랫폼 콜 스택 캡처와 심볼 변환 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) CallStack — 프레임 주소 배열과 해시
    // ------------------------------------------------------------------------------
    /**
     * @brief 캡처한 프레임 주소와 해시입니다. 심볼 변환은 CallStackCapture::symbolize 로 합니다.
     */
    struct SW_API CallStack
    {
        // DeadlockDetector 가 "mutex 를 잠글 때마다" 이 구조체로 캡처하고 복사하므로 얕게 유지한다.
        // 깊은 스택이 필요한 크래시 리포트는 DeepCallStack 을 쓴다.
        static constexpr uint32 kMaxFrames            = 16;
        void*                   _arrFrame[kMaxFrames] = { nullptr };
        uint64                  _hash{ 0 };
        uint32                  _frameCount{ 0 };

        /** @brief 해시 · 프레임 수 · 주소가 모두 같으면 true 입니다. */
        bool operator==( const CallStack& other ) const
        {
            if ( _hash != other._hash || _frameCount != other._frameCount )
                return false;
            for ( uint32 frameIndex = 0; frameIndex < _frameCount; ++frameIndex )
            {
                if ( _arrFrame[frameIndex] != other._arrFrame[frameIndex] )
                    return false;
            }
            return true;
        }

        /** @brief 해시나 프레임이 다르면 true 입니다. */
        bool operator!=( const CallStack& other ) const { return !( *this == other ); }
    };

    /**
     * @brief 크래시 리포트용 깊은 콜 스택입니다.
     * @details CallStack 은 DeadlockDetector 가 잠글 때마다 캡처하고 복사하므로 얕아야 합니다. 반면 크래시는 한 번만 기록하고
     *          깊이가 곧 진단 가치라서 별도 타입으로 둡니다.
     */
    struct SW_API DeepCallStack
    {
        static constexpr uint32 kMaxFrames            = 64;
        void*                   _arrFrame[kMaxFrames] = { nullptr };
        uint32                  _frameCount{ 0 };
    };

} // namespace sw

namespace std
{
    /** @brief CallStack 을 unordered_map 키로 쓸 때의 해시입니다. stack._hash 를 그대로 씁니다. */
    template <>
    struct hash<sw::CallStack>
    {
        /** @brief 미리 계산한 스택 해시를 size_t 로 반환합니다. */
        size_t operator()( const sw::CallStack& stack ) const { return stack._hash; }
    };
} // namespace std

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1-1) 심볼 변환의 **공통 관문** — 플랫폼 구현이 시작하기 전에 함께 거치는 두 곳
    // ------------------------------------------------------------------------------
    /**
     * @brief 프레임이 하나도 없을 때 반환하는 문자열입니다.
     * @details 플랫폼마다 리터럴을 따로 적으면 로그를 grep 하는 쪽이 두 가지 철자를 알아야 합니다.
     */
    inline constexpr auto kEmptyCallStackText = "[Empty CallStack]";

    /**
     * @brief 심볼을 쓸 수 없을 때 **주소만** 찍은 스택 문자열을 만듭니다.
     * @details 심볼 변환은 전역 락을 잡는데, 이 함수는 **크래시 경로에서도 불립니다.** 다른 스레드가 이미 심볼 변환 중이면,
     *          기다리다 교착하는 대신 주소만 남기고 빠져나와야 합니다(주소는 맵 파일로 나중에 풀 수 있습니다). 그 규칙이
     *          플랫폼마다 한 벌씩 적혀 있었습니다. 본체(DbgHelp 와 backtrace_symbols)는 정말로 다르지만 **이 관문만은 같아야
     *          합니다.**
     * @note 일부러 평범한 함수로 둡니다. 크래시 경로에 간접 호출(델리게이트 · 함수 포인터)을 끼워 넣지 않기 위해서입니다.
     */
    SW_API string formatRawCallStackFrames( void* const* ppFrame, uint32 frameCount );

    // ------------------------------------------------------------------------------
    // 2) CallStackCapture — initialize → capture → symbolize → shutdown
    //    initialize 에서 심볼을 로드한다
    // ------------------------------------------------------------------------------
    /** @brief 현재 스레드의 스택을 캡처하고 심볼 문자열로 바꿉니다. */
    class SW_API CallStackCapture
    {
    public:
        /**
         * @brief 심볼 핸들을 로드합니다. capture 전에 한 번 부릅니다.
         */
        static void initialize();

        /**
         * @brief 심볼 핸들을 닫습니다.
         */
        static void shutdown();

        /**
         * @brief 현재 스레드의 콜 스택을 캡처합니다.
         * @param outStack 캡처한 콜 스택을 담을 구조체
         * @param skipFrames 캡처에서 건너뛸 위쪽 프레임 수
         */
        static void capture( CallStack& outStack, uint32 skipFrames = 1 );

        /**
         * @brief 주어진 플랫폼 컨텍스트가 가리키는 지점의 콜 스택을 캡처합니다.
         * @param outStack 캡처한 콜 스택을 담을 구조체
         * @param pPlatformContext Windows 는 `CONTEXT*`. 다른 플랫폼은 이 값을 무시하고 현재 스택을 캡처합니다.
         * @details 크래시 핸들러용입니다. 핸들러 안에서 capture() 를 부르면 예외 디스패치 프레임(KiUserExceptionDispatcher 등)이
         *          앞쪽을 채워서 정작 폴트 지점이 잘립니다. 컨텍스트에서 스택을 따라가면 실제 폴트 프레임이 [0] 에 옵니다.
         */
        static void captureFromContext( DeepCallStack& outStack, void* pPlatformContext );

        /**
         * @brief 캡처한 프레임을 심볼 · 파일 · 줄 번호 문자열로 바꿉니다.
         * @param stack 변환할 콜 스택
         * @return 콜 스택 문자열
         * @details 심볼 API 는 프로세스 전역 락으로 보호됩니다. 락을 바로 얻지 못하면(다른 스레드가 심볼 변환 중이면) 교착을 피해
         *          주소만 출력합니다.
         */
        static string symbolize( const CallStack& stack );
        /** @brief 깊은 콜 스택(크래시 리포트용)을 심볼 문자열로 바꿉니다. */
        static string symbolize( const DeepCallStack& stack );
    };
} // namespace sw
