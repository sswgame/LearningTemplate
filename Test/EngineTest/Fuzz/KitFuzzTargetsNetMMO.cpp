#include "pch.h"

#include "Core/Network/Message/NetMessage.h"
#include "Core/Serialization/BitStream.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

// 퍼징 대상(키트 NetMMO) — 클라이언트가 받는 보임 · 사라짐 · 갱신 메시지(`MMOClientView::handleNetMessage`). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsNetMMOInternal
    {
        static constexpr uint8 kArrKind[] = { NetMMOMessage::kEnter, NetMMOMessage::kLeave, NetMMOMessage::kUpdate };

        /** @brief 입력을 `[종류 고르개 1 바이트][본문]` 의 메시지 하나로 보고 같은 보기에 두 번 먹입니다(보인 엔티티가 있는 갱신 길까지). */
        static void runMMOClientView( const uint8* pData, size_t size )
        {
            if ( pData == nullptr || size == 0 )
                return;
            MMOClientView     view;
            NetMessageContext context;
            context._connectionID = 0;
            for ( size_t pass = 0; pass < 2; ++pass )
            {
                context._kind = kArrKind[( pData[0] + pass ) % std::size( kArrKind )];
                BitReader body( pData + 1, static_cast<int32>( size - 1 ) );
                (void)view.handleNetMessage( context, body ); // 퍼즈 대상 — Malformed 도 정상이다
            }
            vector<MMOClientEvent> listEvent;
            view.drainEvents( listEvent );
        }

        /** @brief 보임(타입 있음) · 갱신(타입 없음)의 엔티티 하나: 번호, [타입], 위치 셋, 상태 덩어리. 사라짐: 개수, 번호들. */
        static void seedMMOClientView( vector<vector<uint8>>& outListSeed )
        {
            const uint8 arrState[] = { 4, 5, 6 };
            for ( uint8 selector = 0; selector < std::size( kArrKind ); ++selector )
            {
                const uint8 kind = kArrKind[selector];
                BitWriter   writer;
                if ( kind == NetMMOMessage::kLeave )
                {
                    writer.writeVarUint( 2 );
                    writer.writeVarUint( 7 );
                    writer.writeVarUint( 8 );
                }
                else
                {
                    writer.writeVarUint( 30 ); // 서버 틱
                    writer.writeBool( true );
                    writer.writeVarUint( 7 );
                    if ( kind == NetMMOMessage::kEnter )
                        writer.writeVarUint( 2 );
                    writer.writeFloat( 1.0f );
                    writer.writeFloat( 0.0f );
                    writer.writeFloat( -3.5f );
                    writer.writeBlob( arrState, sizeof( arrState ) );
                    writer.writeBool( false );
                }
                vector<uint8> seedBytes{ selector };
                seedBytes.insert( seedBytes.end(), writer.getBytes().begin(), writer.getBytes().end() );
                outListSeed.push_back( std::move( seedBytes ) );
            }
        }
    };

    const test::LoaderFuzzTargetRegistrar s_mmoClientViewFuzzTarget{
        test::LoaderFuzzTarget{ "MMOClientView", &KitFuzzTargetsNetMMOInternal::runMMOClientView, &KitFuzzTargetsNetMMOInternal::seedMMOClientView, false }
    };
} // namespace
