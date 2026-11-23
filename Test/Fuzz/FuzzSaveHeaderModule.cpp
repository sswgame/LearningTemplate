// SaveHeader 퍼저의 몸 — 세이브 파일(플레이어 · 모드 · 클라우드가 바꿀 수 있는 바이트)의 봉투(`LocalSealContext::open` — 머리 · 코덱 · 체크섬)와 상태 스냅샷 머리
// (`GameStateSnapshot::readSceneID` — 불러오기가 씬을 정하려고 맨 먼저 읽는 자리)에 임의 바이트를 넣는다. GameFramework 는 엔진이 선 뒤에만 올라오므로(정적 초기화가 이름 풀을 쓴다)
// 퍼저 실행 파일이 링크하지 않고 이 모듈을 이름으로 올린다(부하 봇 `OnlineLoadBotCore` 와 같은 모양).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/ByteStream.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalSealContext.h"

extern "C" SW_MODULE_API void swFuzzSaveHeader( const uint8_t* pData, size_t size )
{
    (void)sw::GameStateSnapshot::readSceneID( pData, static_cast<uint32>( size ) );
    // 장치 키 없이 연다 — 봉인한 봉투는 키 검사에서 거절된다. 봉인하지 않은 봉투는 꼬리 체크섬을 맞춰 준다(틀리면 첫 검사에서 버려 코덱 · 크기 상한을 보지 못한다).
    constexpr size_t  kChecksumSize = sizeof( uint32 );
    sw::vector<uint8> envelope( pData, pData + size );
    if ( envelope.size() >= sw::LocalSealContext::kHeaderSize + kChecksumSize )
    {
        envelope.resize( envelope.size() - kChecksumSize );
        const uint32 checksum = sw::HashUtil::computeCRC32( envelope.data(), envelope.size() );
        sw::ByteWriter( envelope ).writeUint32( checksum );
    }
    sw::vector<uint8> body;
    uint32            formatVersion = 0;
    if ( sw::LocalSealContext::open( sw::LocalSealContext{}, envelope.data(), envelope.size(), body, formatVersion ) == sw::LocalStoreResult::Ok )
        (void)sw::GameStateSnapshot::readSceneID( body.data(), static_cast<uint32>( body.size() ) );
}
