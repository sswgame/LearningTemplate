#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioClipDecoder.h"

// AudioVorbisTest — 저장소의 실제 .ogg 를 디코더가 끝까지 푸는지(nogpu).
// 약어 일괄 변경이 Ogg 페이지 매직 "OggS" 를 "OGGS" 로 바꿔 모든 .ogg 가 디코드에 실패했다(1dafbae0c9) — 형식 매직은 바깥 규격이라 이름 규칙의 대상이 아니다.

/**
 * @brief [AudioVorbisTest] 실제 Vorbis 파일을 풀면 채널 · 표본율 · PCM 바이트가 있다
 */
SW_TEST_CASE( AudioVorbisTest, DecodesARealOggFile )
{
    const sw::string  path = sw::ProcessRoots::get()._project.str() + "/Content/game/shooter3d/sounds/footstep_concrete_000.ogg";
    sw::vector<uint8> bytes;
    SW_REQUIRE_TRUE( sw::FileUtil::readFile( path, bytes ) );
    SW_REQUIRE_TRUE( bytes.size() > 4 );

    sw::AudioPcm pcm;
    SW_REQUIRE_TRUE( sw::AudioClipDecoder::decodeOGG( bytes.data(), bytes.size(), pcm ) );
    SW_EXPECT_TRUE( pcm._channelCount > 0 );
    SW_EXPECT_TRUE( pcm._sampleRate > 0 );
    SW_EXPECT_TRUE( pcm._listData.empty() == false );
}
