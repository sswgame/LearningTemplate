/**
 * @file OnlineStoreDriverTable.h
 * @brief 서버 조립이 서버 설정의 저장소와 캐시 항목(드라이버 이름)으로 실제 드라이버를 만드는 함수 테이블입니다.
 * @details - 드라이버(SQLite, PostgreSQL, RESP)는 저장 키트(`GF_Server_SQLStore`, `GF_Server_CacheStore`)에 있고, 기반은 키트를 모릅니다.
 *            그래서 저장 키트가 모듈 로드 때 만들기 함수를 이 테이블에 묶고, 서버 조립은 이 테이블로 만듭니다.
 *          - `memory` 드라이버는 테이블과 상관없이 서버 조립이 직접 만듭니다(그 런타임 안에서만 나눠 쓰는 메모리 구현).
 *          - 묶인 함수가 없는데 다른 드라이버를 고르면 서버 조립은 기동을 멈춥니다. 다른 드라이버로 대신하지 않습니다.
 *          - 키트는 묶기를 손으로 부르지 않고 `.cpp` 의 정적 `OnlineStoreDriverBinding` 하나로 둡니다. 모듈이 로드되면 묶이고 언로드되면 풀려서,
 *            "키트는 실렸는데 묶지 않았다" 나 "풀지 않아 언로드된 함수를 부른다" 가 생기지 않습니다(키트 조립의 `SW_REGISTER_ONLINE_SERVER_KIT` 와 같은 모양).
 *          - 풀기는 자기 함수가 칸에 있을 때만 지웁니다. 핫 리로드는 옛 이미지의 언로드를 미뤄 새 이미지가 먼저 묶을 수 있어,
 *            옛 묶음의 소멸자가 칸을 무조건 비우면 새 이미지의 함수가 사라집니다. 테이블은 모듈 언로드 리스너라 내려가는 이미지의 함수도 지웁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IEphemeralStore;
    class IServiceStore;

    /**
     * @struct OnlineStoreDriverTable
     * @brief 저장 드라이버 만들기 함수 테이블입니다. 함수 하나씩이며, 모듈 로드(묶기)와 언로드(풀기)에서 바뀝니다.
     */
    struct SW_GF_API OnlineStoreDriverTable
    {
        /** @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. @p secret 은 쓰고 나면 부른 쪽이 비웁니다. */
        using CreateServiceStoreFunction = unique_ptr<IServiceStore> ( * )( string_view driverName, string_view connection, string_view secret, int32 workerCount,
                                                                            UnixMilliseconds nowMs, string& outError );
        /** @brief 캐시를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. */
        using CreateEphemeralStoreFunction = unique_ptr<IEphemeralStore> ( * )( string_view driverName, string_view endpoint, string_view secret, string& outError );

        static constexpr const utf8* kMemoryDriverName = "memory";

        /**
         * @brief 저장소 만들기 함수를 묶고 그 묶음을 돌려줍니다. 이미 묶인 함수가 있으면 바꿉니다(핫 리로드의 새 이미지).
         * @details 구독이 풀리면 칸이 아직 @p pCreate 를 들고 있을 때만 비웁니다 — 그 뒤에 다른 이미지가 묶은 함수는 남깁니다.
         *          묶기와 풀기는 모듈 로드 · 언로드 스레드에서 합니다(구독 목록에 잠금이 없다). 칸 읽기는 원자라 어느 스레드에서나 됩니다.
         */
        [[nodiscard]] static Subscription   bindServiceStoreFactory( CreateServiceStoreFunction pCreate );
        [[nodiscard]] static Subscription   bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate );
        static CreateServiceStoreFunction   findServiceStoreFactory();
        static CreateEphemeralStoreFunction findEphemeralStoreFactory();
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineStoreDriverBinding
     * @brief 저장 키트의 만들기 함수를 모듈 수명 동안 테이블에 묶는 정적 객체입니다. 키트 `.cpp` 의 `namespace sw` 에 하나 둡니다.
     * @details @p pRelease 는 해제할 때(모듈 언로드) 부르는 정리 함수입니다. 드라이버 레지스트리에서 이 모듈의 드라이버를 해제하는 일 같은 것입니다. 없으면 nullptr 입니다.
     *          소멸자는 자기가 묶은 함수만 풉니다 — 그사이 새 이미지가 묶은 함수는 그대로 둡니다.
     */
    class OnlineStoreDriverBinding
    {
    public:
        using ReleaseFunction = void ( * )();

        OnlineStoreDriverBinding( OnlineStoreDriverTable::CreateServiceStoreFunction pCreate, ReleaseFunction pRelease )
            : _binding{ OnlineStoreDriverTable::bindServiceStoreFactory( pCreate ) }
            , _pRelease{ pRelease }
        {
        }
        OnlineStoreDriverBinding( OnlineStoreDriverTable::CreateEphemeralStoreFunction pCreate, ReleaseFunction pRelease )
            : _binding{ OnlineStoreDriverTable::bindEphemeralStoreFactory( pCreate ) }
            , _pRelease{ pRelease }
        {
        }
        ~OnlineStoreDriverBinding()
        {
            _binding.reset(); // 정리 함수보다 먼저 푼다 — 정리 뒤에 이 함수로 드라이버를 만들지 않게
            if ( _pRelease != nullptr )
                _pRelease();
        }

        OnlineStoreDriverBinding( const OnlineStoreDriverBinding& )            = delete;
        OnlineStoreDriverBinding& operator=( const OnlineStoreDriverBinding& ) = delete;

    private:
        Subscription    _binding; ///< 칸에 묶은 자기 함수. 풀리면 칸이 아직 그 함수일 때만 비운다
        ReleaseFunction _pRelease;
    };
} // namespace sw
