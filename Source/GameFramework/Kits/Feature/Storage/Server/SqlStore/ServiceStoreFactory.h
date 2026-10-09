/**
 * @file ServiceStoreFactory.h
 * @brief 서버 설정의 저장소 항목(`ServerStoreEntry` — 드라이버 이름 · 접속 글 · 비밀) 하나 → 서비스 저장소입니다.
 * @details "memory" 는 기반의 메모리 구현(시험 · 개발 — 같은 프로세스의 앞들이 데이터 하나를 나눠 쓴다), 그 밖의 이름은 `SqlServiceStore`(드라이버 등록부에서 찾는다).
 *          이 빌드 타깃에 없는 드라이버면 분명한 오류로 기동을 멈춘다(다른 드라이버로 바꾸지 않는다).
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
     * @brief 저장소 공장입니다.
     */
    struct SW_GF_API ServiceStoreFactory
    {
        static constexpr const utf8* kMemoryDriverName = "memory";

        /**
         * @brief 저장소를 만들고 띄웁니다. 실패하면 nullptr 과 까닭입니다.
         * @param secret 비밀번호(`ServerSecret::read` 로 읽은 것) — 쓰고 나면 부르는 쪽이 비운다
         * @param nowMs 마이그레이션 기록에 남길 벽시계 밀리초
         */
        static unique_ptr<IServiceStore> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                             string& outError );
        /** @brief 이 모듈이 올린 드라이버를 등록부에서 내립니다(모듈을 내리기 전에). */
        static void shutdown();
    };
} // namespace sw
