/**
 * @file ServerServiceManager.h
 * @brief 레지스트리에 올라온 서버 서비스의 인스턴스를 만들고, 돌리고, 멈춥니다. 전용 서버 실행 파일이 하나 가집니다.
 * @details - 시작은 시작 순서대로, 멈춤은 그 반대로 합니다. 하나라도 시작에 실패하면 이미 시작한 것을 모두 멈추고 실패를 알립니다(서버는 기동하지 않는다).
 *          - 서비스 코드는 그것을 올린 모듈(DLL)에 있습니다. 그 모듈을 언로드하기 **전에** `stopAll` 을 불러야 합니다. 서버는 모듈 리로드 직전과 종료 때 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Observability/ServerServiceRegistry.h"

namespace sw
{
    /**
     * @class ServerServiceManager
     * @brief 서버 서비스 인스턴스의 수명을 정합니다. 게임 스레드 하나에서 씁니다.
     */
    class SW_API ServerServiceManager
    {
    public:
        ServerServiceManager();
        ~ServerServiceManager();

        ServerServiceManager( const ServerServiceManager& )            = delete;
        ServerServiceManager& operator=( const ServerServiceManager& ) = delete;

        /**
         * @brief 올라온 팩토리마다 인스턴스를 만들어 시작합니다. 이미 돌고 있으면 아무것도 하지 않고 true 입니다.
         * @return 하나라도 실패하면 시작한 것을 모두 멈추고 false 입니다(오류 로그에 서비스 이름과 이유).
         */
        [[nodiscard]] bool startAll( const ServerServiceContext& context );
        /** @brief 시작의 반대 순서로 멈추고 인스턴스를 지웁니다. 두 번 불러도 됩니다. */
        void stopAll();
        /** @brief 돌고 있는 서비스마다 `tick` 을 부릅니다. */
        void tick( int64 nowMs );

        bool  isRunning() const { return _bRunning == SW_TRUE; }
        int32 getRunningCount() const { return static_cast<int32>( _listRunning.size() ); }
        /** @brief 돌고 있는 서비스의 연결 수 합입니다. */
        int32 getConnectionCount() const;

    private:
        struct RunningService
        {
            unique_ptr<IServerService> _service{};
            string                     _name{};
        };

        vector<RunningService> _listRunning; ///< 시작 순서
        uint8                  _bRunning;
    };
} // namespace sw
