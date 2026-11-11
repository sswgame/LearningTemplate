/**
 * @file OnlineEndpointTestUtil.h
 * @brief 설정으로 고른 제공자를 로컬 가짜 끝점까지 잇는 끝단 테스트의 공용 부품입니다. 환경 변수 비밀, 제공자 파일 쓰기, 시한까지 돌리기를 한 벌로 둡니다.
 * @details 스위트 파일이 아니다 — 저장소 · 캐시 · 영수증 · 푸시 끝단 테스트(키트 DLL)가 include 한다. 실제 계정 · 키는 쓰지 않고 키는 케이스마다 새로 만든다(`INetSecurityProvider`).
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

namespace sw::test
{
    /** @brief 케이스 동안만 환경 변수 하나를 정합니다. 운영과 같은 길(`ServerSecret::read`)로 비밀을 넘기려는 것입니다. */
    class ScopedEnvironmentVariable final
    {
    public:
        ScopedEnvironmentVariable( string_view name, string_view value )
            : _name{ name }
        {
            SW_EXPECT_TRUE( ProcessEnvironment::trySet( name, value ) );
        }
        ~ScopedEnvironmentVariable() { SW_EXPECT_TRUE( ProcessEnvironment::tryUnset( _name ) ); }

        ScopedEnvironmentVariable( const ScopedEnvironmentVariable& )            = delete;
        ScopedEnvironmentVariable& operator=( const ScopedEnvironmentVariable& ) = delete;

    private:
        string _name;
    };

    /** @brief 끝단 테스트가 실제 소켓을 기다리는 시한입니다. 넉넉히 두되 통과하는 케이스는 왕복만큼만 걸립니다. */
    inline constexpr int64 kEndpointTimeoutMs = 20000;

    /**
     * @brief @p isDone 이 참이 될 때까지 @p pump 를 돌립니다. 실제 소켓의 답은 시간이 들어 라운드 수로 셀 수 없습니다.
     * @return 시한 안에 끝났으면 true 입니다.
     */
    template <typename TPump, typename TDone>
    [[nodiscard]] bool pollUntil( TPump&& pump, TDone&& isDone )
    {
        const Deadline deadline = Deadline::afterMilliseconds( kEndpointTimeoutMs );
        while ( isDone() == false )
        {
            if ( deadline.isExpired() )
                return false;
            pump();
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + constant::time::kNanosecondsPerMillisecond );
        }
        return true;
    }

    /** @brief 케이스 폴더에 글 파일 하나를 쓰고 절대 경로를 돌려줍니다(제공자 파일 · 신뢰 PEM). */
    [[nodiscard]] inline string writeScratchText( string_view name, string_view text )
    {
        string path = FileUtil::normalizeSeparators( makeScratchPath( name ) );
        SW_EXPECT_TRUE( FileUtil::writeTextFile( path, text ) );
        return path;
    }

    /** @brief 글을 JSON 문자열 안에 넣을 수 있게 고칩니다(PEM 의 줄바꿈 · 경로의 역슬래시). */
    [[nodiscard]] inline string escapeJSONText( string_view text )
    {
        string escaped;
        escaped.reserve( text.size() );
        for ( const utf8 character : text )
        {
            if ( character == '\n' )
            {
                escaped += "\\n";
                continue;
            }
            if ( character == '\\' || character == '"' )
                escaped.push_back( '\\' );
            escaped.push_back( character );
        }
        return escaped;
    }
} // namespace sw::test
