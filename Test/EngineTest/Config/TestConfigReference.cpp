#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Json/JsonDocument.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>

/**
 * @brief [ConfigReferenceTest] 생성 문서(`docs/Config/ConfigReference.json`)의 설정 칸이 리플렉션 등록과 같다 — 문서 생성기는 헤더를 모양으로 읽으므로,
 *        놓치거나 지어낸 칸이 있으면 여기서 진다
 * @details 이 실행 파일에 등록된 타입만 본다(엔진 · GameFramework). 에디터 타입과 손으로 읽는 키 표는 `TypeRegistry` 에 없어 건너뛴다.
 *          생성기는 인쇄기이고 판정은 리플렉션이 한다(생성기는 빌드 없이 돌아야 커밋 훅이 낡은 문서를 잡는다).
 */
SW_TEST_CASE( ConfigReferenceTest, FieldsMatchReflection )
{
    const sw::string path = sw::FileUtil::joinPath( sw::ResourceUtil::getProjectFolderPath(), "docs/Config/ConfigReference.json" );
    sw::JsonDocument doc;
    SW_ASSERT_TRUE_MSG( doc.loadPath( path ), path.c_str() );

    uint32              comparedCount{ 0 };
    const sw::JsonValue listFile = doc.getRoot().get( "files" );
    for ( size_t fileIndex = 0; fileIndex < listFile.size(); ++fileIndex )
    {
        const sw::JsonValue listType = listFile.at( fileIndex ).get( "types" );
        for ( size_t typeIndex = 0; typeIndex < listType.size(); ++typeIndex )
        {
            const sw::JsonValue typeEntry = listType.at( typeIndex );
            const sw::string    typeName  = typeEntry.get( "name" ).asString();
            const sw::TypeInfo* pType     = sw::engine::getTypeRegistry().findType( sw::hashed_string( typeName ) );
            if ( pType == nullptr )
                continue;

            sw::vector<sw::string> listDocName;
            const sw::JsonValue    listField = typeEntry.get( "fields" );
            for ( size_t fieldIndex = 0; fieldIndex < listField.size(); ++fieldIndex )
            {
                listDocName.push_back( listField.at( fieldIndex ).get( "name" ).asString() );
            }
            sw::vector<sw::string> listReflectedName;
            for ( const sw::PropertyInfo& prop : pType->getPropertiesWithBase() )
            {
                listReflectedName.push_back( prop._name.c_str() );
            }
            std::sort( listDocName.begin(), listDocName.end() );
            std::sort( listReflectedName.begin(), listReflectedName.end() );

            sw::string docText;
            sw::string reflectedText;
            for ( const sw::string& name : listDocName )
            {
                docText += name + " ";
            }
            for ( const sw::string& name : listReflectedName )
            {
                reflectedText += name + " ";
            }
            SW_EXPECT_TRUE_MSG( docText == reflectedText,
                                typeName + ": 문서 [" + docText + "] / 리플렉션 [" + reflectedText + "] - Scripts/common/ConfigReference.py 가 선언을 잘못 읽었다" );
            ++comparedCount;
        }
    }
    // EngineConfig · WindowConfig · GameConfig · EngineDefaultAssets · PhysicsSettings(+2) · NavMeshSettings(+2) · GameSettings · ServerConfig(+2)
    SW_EXPECT_TRUE_MSG( comparedCount >= 10u, "대조한 타입이 " + sw::to_string( comparedCount ) + " 개뿐입니다" );
}
