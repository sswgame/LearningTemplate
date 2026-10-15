#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

#include "Games/Empty/BenchSceneSettings.h"

namespace sw
{
    namespace
    {
        struct BenchSceneSettingsDataKindInternal
        {
            [[nodiscard]] static bool loadBenchSceneSettings( const string& resourceID )
            {
                BenchSceneSettings settings;
                return ReflectedXMLFile::loadDesc( resourceID, settings );
            }
        };
    } // namespace

    /** @brief 벤치 장면 설정(`BenchSceneSettings`)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_RESOURCE_DATA_KIND( BenchSceneSettings, "benchscene", "game/", nullptr, "/data/benchscene.xml", &BenchSceneSettingsDataKindInternal::loadBenchSceneSettings );
} // namespace sw
