/**
 * @file FixedNetWireVersion.h
 * @brief 넷 채널이 고정 배치형(B)으로 싣는 메시지 타입의 스키마 해시를 핸드셰이크 버전(`NetHostConfig::_wireVersion`)에 섞습니다.
 * @details 계획은 `docs/plans/ReflectionFormats.md` 5절 "게임 넷 연결"입니다.
 *          게임은 호스트를 열기 전에 채널에 싣는 B 메시지 타입을 모두 여기 적습니다. 타입 하나의 필드, 순서, 범위 키, 열거자가 바뀌면 해시가 바뀌고,
 *          두 빌드의 프로토콜 ID 가 갈려 핸드셰이크가 `NetDisconnectReason::VersionMismatch` 로 거절합니다. 손 버전을 올리지 않아도 됩니다.
 *          메시지 바이트에도 스트림 머리의 해시가 있으므로(`FixedLayout::write`), 여기 적지 않은 타입이 다른 빌드에서 오면 읽기가 `SchemaMismatch` 로 거절합니다.
 *          조용히 잘못 읽는 경로는 어느 쪽에도 없습니다.
 *
 *          코덱이 없는 타입을 적으면 `FixedLayout::getSchemaHash` 가 이유를 남기고 프로세스를 멈춥니다. 호스트를 여는 기동 코드에서 드러납니다.
 * @code
 *     settings._wireVersion = FixedNetWireVersion::combine< MechArenaSnapshot >( NetWireVersion::combine( { NetKitWireVersion::kClientServer, kMyGameWireVersion } ) );
 * @endcode
 */
#pragma once
#include "Core/Network/NetTypes.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace sw
{
    /** @brief B 메시지 타입의 스키마 해시를 와이어 버전에 섞는 진입점입니다. */
    struct FixedNetWireVersion
    {
        /** @brief @p layerVersion(키트와 게임의 손 버전을 묶은 값)에 @p TMessage 의 스키마 해시를 적은 순서대로 섞습니다. */
        template <typename... TMessage>
        [[nodiscard]] static uint32 combine( const uint32 layerVersion )
        {
            static_assert( sizeof...( TMessage ) > 0, "list the fixed layout message types the channel carries" );
            uint32 version = layerVersion;
            ( ( version = NetWireVersion::mixSchemaHash( version, FixedLayout::getSchemaHash<TMessage>() ) ), ... );
            return version;
        }
    };
} // namespace sw
