/**
 * @file CallStackCapture.h
 * @brief 크로스플랫폼 콜 스택 캡처 및 심볼 변환 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) CallStack — 프레임 포인터 배열과 해시
    // ------------------------------------------------------------------------------
    /**
     * @brief 캡처된 프레임 주소와 해시입니다. 심볼화는 CallStackCapture::symbolize.
     */
    struct SW_API CallStack
    {
        // DeadlockDetector 가 "모든 mutex 잠금마다" 이 구조체로 캡처·복사하므로 얕게 유지한다.
        // 깊은 스택이 필요한 크래시 리포트는 DeepCallStack 을 쓴다.
        static constexpr uint32 kMaxFrames            = 16;
        void*                   _arrFrame[kMaxFrames] = { nullptr };
        uint64                  _hash{ 0 };
        uint32                  _frameCount{ 0 };

        /** @brief 해시·프레임 수·주소가 모두 같으면 true 입니다. */
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

        /** @brief 해시 또는 프레임이 다르면 true 입니다. */
        bool operator!=( const CallStack& other ) const { return !( *this == other ); }
    };

    /**
     * @brief 크래시 리포트용 깊은 콜 스택입니다.
     * @details CallStack 은 DeadlockDetector 가 모든 잠금마다 캡처·복사하므로 얕아야 합니다.
     *          반면 크래시는 한 번만 찍고 깊이가 곧 진단 가치라 별도 타입으로 둡니다.
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
    /** @brief CallStack 을 unordered_map 키로 쓸 때 해시는 stack._hash 입니다. */
    template <>
    struct hash<sw::CallStack>
    {
        /** @brief 미리 계산된 스택 해시를 size_t 로 돌려줍니다. */
        size_t operator()( const sw::CallStack& stack ) const { return stack._hash; }
    };
} // namespace std

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) CallStackCapture — initialize → capture → symbolize → shutdown
    //    initialize 에서 심볼을 로드합니다
    // ------------------------------------------------------------------------------
    /** @brief 현재 스레드 스택을 캡처하고 심볼 문자열로 바꿉니다. */
    class SW_API CallStackCapture
    {
    public:
        /**
         * @brief 심볼 핸들을 로드합니다. capture 전에 한 번 호출합니다.
         */
        static void initialize();

        /**
         * @brief 심볼 핸들을 닫습니다.
         */
        static void shutdown();

        /**
         * @brief 현재 스레드의 콜 스택을 캡처합니다.
         * @param outStack 캡처된 콜 스택을 저장할 구조체
         * @param skipFrames 캡처를 건너뛸 상단 프레임 수
         */
        static void capture( CallStack& outStack, uint32 skipFrames = 1 );

        /**
         * @brief 주어진 플랫폼 컨텍스트가 가리키는 지점의 콜 스택을 캡처합니다.
         * @param outStack 캡처된 콜 스택을 저장할 구조체
         * @param pPlatformContext Windows 는 `CONTEXT*`. 다른 플랫폼은 무시하고 현재 스택을 캡처합니다.
         * @details 크래시 핸들러용입니다. 핸들러 안에서 capture() 를 부르면 예외 디스패치
         *          프레임(KiUserExceptionDispatcher 등)이 앞을 채워 정작 폴트 지점이 잘립니다.
         *          컨텍스트에서 걸어가면 실제 폴트 프레임이 [0] 에 옵니다.
         */
        static void captureFromContext( DeepCallStack& outStack, void* pPlatformContext );

        /**
         * @brief 캡처된 프레임을 심볼·파일·라인 문자열로 바꿉니다.
         * @param stack 변환할 콜 스택
         * @return 콜 스택 문자열
         * @details 심볼 API 는 프로세스 전역 락으로 보호됩니다. 락을 즉시 얻지 못하면
         *          (다른 스레드가 심볼화 중) 교착을 피해 주소만 출력합니다.
         */
        static string symbolize( const CallStack& stack );
        /** @brief 깊은 콜 스택(크래시 리포트)을 심볼 문자열로 바꿉니다. */
        static string symbolize( const DeepCallStack& stack );
    };
} // namespace sw
