/**
 * @file OnlineProtocolRegistry.h
 * @brief 온라인 키트 프로토콜의 등록 항목과 레지스트리입니다. 접속 핸드셰이크의 첫 필드인 온라인 스키마 해시가 여기서 나옵니다.
 * @details - 키트는 Shared `.cpp` 의 이름 공간 범위 객체로 `OnlineProtocolEntry` 하나를 둡니다. 객체를 만들면 레지스트리에 오르고, 모듈이 내려가며 파괴되면 내립니다.
 *          - 키트 서비스(클라이언트 `IOnlineClientService` · 서버 `IOnlineHostService`)는 `getProtocol` 로 이 항목을 돌려줘야 합니다.
 *            그래서 항목 없이는 서비스를 만들 수 없고, 서비스를 가진 키트는 빠짐없이 해시에 섞입니다. 키트가 따로 목록을 적는 곳은 없습니다.
 *          - 해시는 이 빌드에 올라온 항목 전부를 섞습니다. 클라이언트가 키트 일부만 올려도 서버와 같은 값이 나와야 하기 때문입니다.
 *            서버와 클라이언트는 같은 빌드로 배포하므로 같은 키트 모듈이 오르고 같은 해시가 나옵니다(`docs/plans/ReflectionFormats.md` 5절).
 *          - 지금은 영역과 손 버전(`kVersion`)을 섞습니다. 메시지 스키마 해시는 단계 5-b 에서 항목에 더해 같은 자리에서 섞습니다.
 *          언리얼 `FNetworkVersion::GetLocalNetworkVersion`(빌드에서 나온 값 하나를 접속 때 비교)과 같은 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Observability/ServiceFactoryList.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class OnlineProtocolEntry
     * @brief 키트 프로토콜 하나의 등록 항목입니다. 만들면 오르고 파괴되면 내립니다. 키트는 Shared `.cpp` 의 이름 공간 범위에 둡니다.
     * @details 모듈이 로드될 때 오르고 내려갈 때 내려야 빌드의 키트 목록과 같아집니다. 함수 안의 정적 객체로 두면 처음 부를 때에야 올라 클라이언트와 서버가 어긋납니다.
     */
    class SW_GF_API OnlineProtocolEntry final : private ServiceFactoryNode
    {
    public:
        /** @param pName 로그에 쓰는 이름(문자열 리터럴). 빌드 안에서 하나뿐이어야 합니다. */
        OnlineProtocolEntry( const utf8* pName, uint16 methodRange, uint32 version );
        ~OnlineProtocolEntry();

        OnlineProtocolEntry( const OnlineProtocolEntry& )            = delete;
        OnlineProtocolEntry& operator=( const OnlineProtocolEntry& ) = delete;

        uint16 getMethodRange() const { return _methodRange; }

    private:
        friend struct OnlineProtocolRegistry;

        uint32 _version;
        uint16 _methodRange;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OnlineProtocolRegistry
     * @brief 키트 프로토콜 항목의 레지스트리입니다. GameFramework 에 하나 있고 키트 모듈이 로드될 때 항목이 오릅니다.
     */
    struct SW_GF_API OnlineProtocolRegistry
    {
        /**
         * @brief 올라온 항목 전부와 `OnlineProtocolConstant::kBaseVersion` 을 섞은 온라인 스키마 해시입니다.
         * @details 영역 순(같으면 이름 순)으로 섞으므로 모듈을 올린 순서와 상관없이 같은 값입니다. 호스트와 클라이언트가 `initialize` 때 한 번 부릅니다.
         */
        [[nodiscard]] static uint64 computeSchemaHash();

    private:
        friend class OnlineProtocolEntry;

        static void registerEntry( OnlineProtocolEntry* pEntry );
        static void unregisterEntry( OnlineProtocolEntry* pEntry );
    };
} // namespace sw
