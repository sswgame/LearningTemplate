/**
 * @file ServiceStoreFactory.h
 * @brief 서버 설정의 저장소 항목(`ServerStoreEntry`) 하나로 서비스 저장소를 만듭니다. 저장소 항목은 드라이버 이름, 접속 문자열, 비밀로 이루어집니다.
 * @details "memory" 는 기반 계층의 메모리 구현이며 테스트와 개발에 쓴다. 같은 프로세스의 저장소끼리 데이터를 나눠 쓴다.
 *          그 밖의 이름은 `SQLServiceStore` 로 만들며 드라이버 레지스트리에서 드라이버를 찾는다.
 *          이 빌드 타깃에 없는 드라이버이면 분명한 오류를 남기고 기동을 멈추며, 다른 드라이버로 대신하지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IServiceStore;

    /**
     * @struct ServiceStoreFactory
     * @brief 저장소 팩토리입니다.
     */
    struct SW_KIT_SERVER_API ServiceStoreFactory
    {
        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param secret 비밀번호(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         * @param nowMs 마이그레이션 기록에 남길 벽시계 시각(밀리초)입니다.
         */
        static unique_ptr<IServiceStore> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                             string& outError );
    };
} // namespace sw
