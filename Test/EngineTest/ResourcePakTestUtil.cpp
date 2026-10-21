#include "pch.h"

#include "EngineTest/ResourcePakTestUtil.h"

#include "Engine/Resource/Pack/ResourcePakWriter.h"

namespace sw::test
{
    bool ResourcePakTestUtil::createPackFile( const string& packPath, uint32 dlcAppID, PackCompressionType compression, const vector<pair<string, string>>& listFileContent )
    {
        // 쓰기는 쿠커와 같은 `ResourcePakWriter` 하나다 — 테스트가 레이아웃을 따로 들면 쿠커가 쓰지 않는 팩을 읽는 테스트가 된다.
        ResourcePakWriterDesc desc;
        desc._compression = compression;
        desc._dlcAppID    = dlcAppID;
        ResourcePakWriter writer( desc );
        for ( const auto& [relPath, content] : listFileContent )
        {
            const auto* pContentBytes = reinterpret_cast<const uint8*>( content.data() );
            writer.addBytes( relPath, vector<uint8>( pContentBytes, pContentBytes + content.size() ) );
        }
        return writer.writePackFile( packPath );
    }
} // namespace sw::test
