#include "pch.h"

#include "Core/Network/Message/NetMessage.h"
#include "Core/Serialization/BitStream.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"

// 퍼징 대상(키트 NetDestruction) — 클라이언트가 받는 이벤트 · 스냅숏 · 자세 · 해시 메시지(`DestructionReplicationClient::handleNetMessage`).
// 등록한 파괴 오브젝트가 없으므로 번호 뒤를 읽는 것은 자세 메시지다(오브젝트를 세우려면 물리 월드가 필요하다). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsNetDestructionInternal
    {
        static constexpr uint8 kArrKind[] = { NetDestructionMessage::kEvent, NetDestructionMessage::kSnapshot, NetDestructionMessage::kPose,
                                              NetDestructionMessage::kHash };

        /** @brief 입력을 `[종류 고르개 1 바이트][본문]` 의 메시지 하나로 봅니다. */
        static void runDestructionReplication( const uint8* pData, size_t size )
        {
            if ( pData == nullptr || size == 0 )
                return;
            DestructionReplicationClient client;
            NetMessageContext            context;
            context._connectionID = 0;
            context._kind         = kArrKind[pData[0] % std::size( kArrKind )];
            BitReader body( pData + 1, static_cast<int32>( size - 1 ) );
            (void)client.handleNetMessage( context, body ); // 퍼즈 대상 — Malformed 도 정상이다
        }

        /** @brief 멈춘 자세(비트 그대로) · 움직이는 자세(양자화, 원점 · 단위 사원수는 늘 범위 안) 메시지입니다: 멈춤, 번호, 틱, 개수, (그룹, 자세)… */
        static void seedDestructionReplication( vector<vector<uint8>>& outListSeed )
        {
            static constexpr uint8 kPoseSelector = 2;
            for ( int32 rest = 0; rest < 2; ++rest )
            {
                BitWriter writer;
                writer.writeBool( rest != 0 );
                writer.writeVarUint( 1 );  // 번호
                writer.writeVarUint( 40 ); // 틱
                writer.writeVarUint( 1 );  // 개수
                writer.writeVarUint( 3 );  // 그룹
                if ( rest != 0 )
                {
                    const float32 arrPose[] = { 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f };
                    for ( const float32 value : arrPose )
                    {
                        writer.writeFloat( value );
                    }
                }
                else
                {
                    writer.writeUint32( 0 ); // 양자화한 칸 — 범위를 넘으면 읽기가 거절한다(변이가 경계를 찾는다)
                    writer.writeUint32( 0 );
                    writer.writeUint32( 0 );
                }
                vector<uint8> seedBytes{ kPoseSelector };
                seedBytes.insert( seedBytes.end(), writer.getBytes().begin(), writer.getBytes().end() );
                outListSeed.push_back( std::move( seedBytes ) );
            }
            // 등록되지 않은 번호의 이벤트 · 해시 — 소비하고 버리는 길.
            for ( uint8 selector = 0; selector < std::size( kArrKind ); ++selector )
            {
                if ( selector == kPoseSelector )
                    continue;
                const vector<uint8> seedBytes{ selector, 9 };
                outListSeed.push_back( seedBytes );
            }
        }
    };

    const test::LoaderFuzzTargetRegistrar s_destructionReplicationFuzzTarget{
        test::LoaderFuzzTarget{ "DestructionReplication", &KitFuzzTargetsNetDestructionInternal::runDestructionReplication,
                               &KitFuzzTargetsNetDestructionInternal::seedDestructionReplication, false }
    };
} // namespace
