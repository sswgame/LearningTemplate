#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Graphics/Material/MaterialTagIndex.h"

namespace sw
{
    namespace
    {
        /** @brief 머티리얼 값을 런타임에 바꾸고(`matswitch` · `matparam`) 읽지 않은 머티리얼을 태그로 찾는(`matfind`) 개발 명령입니다. */
        struct MaterialCommandsInternal
        {
            static constexpr size_t kMaxListedMaterial = 16; ///< `matfind` 답에 적는 머티리얼 수 상한(수는 모두 센다)

            /**
             * @brief 지금 읽는 콘텐츠의 도메인(낮은 우선순위가 앞)입니다. 쿠킹본은 마운트한 도메인 팩이고, 소스는 팩 쿠커가 굽는 도메인(`engine` · `common` · `game/<팩>`)이다.
             * @return 목록을 끝까지 내지 못하면 false 입니다.
             */
            [[nodiscard]] static bool collectContentDomains( vector<string>& outListDomain )
            {
                if ( AssetIO::getContentSource() == ContentSource::Cooked )
                {
                    const PackManager* const pPacks = engine::findSubsystem<PackManager>();
                    if ( pPacks == nullptr )
                        return false;
                    outListDomain = pPacks->getMountedDomainsByPriority();
                    return true;
                }
                outListDomain = { "engine", "common" };
                return AssetPaths::collectGamePackDomains( engine::getSubsystem<AssetPaths>().getRootFolderPath(), outListDomain );
            }

            /**
             * @brief `matfind <keyword|shader> <값> [머티리얼]` — 키워드(define 이름)를 켠 머티리얼이나 그 셰이더로 그리는 머티리얼을 찾습니다(`MaterialTagIndex`). 읽지 않은 머티리얼도 든다.
             * @details 유니티 에디터의 "이 셰이더 키워드를 쓰는 머티리얼 찾기", 언리얼 에셋 레지스트리 태그 질의 자리다. 쿠킹본은 팩의 색인을, 소스는 머티리얼을 읽어 같은 줄을 만든다.
             *          셋째 인자(리소스 id)를 주면 그 머티리얼이 결과에 없을 때 진다 — 시나리오가 색인을 확인한다.
             */
            static bool runFind( const vector<string>& listArgument, string& outReply )
            {
                const bool bArgumentCount = listArgument.size() == 2 || listArgument.size() == 3;
                const bool bKeyword       = bArgumentCount && listArgument[0] == "keyword";
                const bool bShader        = bArgumentCount && listArgument[0] == "shader";
                if ( bKeyword == false && bShader == false )
                {
                    outReply = "usage: matfind <keyword|shader> <value> [material]";
                    return false;
                }
                vector<string> listDomain;
                if ( collectContentDomains( listDomain ) == false )
                {
                    outReply = "the content domains could not be listed";
                    return false;
                }
                const expected<MaterialTagIndex> index = MaterialTagIndex::loadMounted( listDomain );
                if ( index.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "material index: %# '%#'", index.error().message(), index.error().detail() );
                    outReply = builder.view();
                    return false;
                }
                const vector<string>                      listFound = bKeyword ? index->findByKeyword( listArgument[1] ) : index->findByShaderPath( listArgument[1] );
                StringBuilder<constant::buffer::kMax1024> builder;
                builder.appendFormat( "%# materials", listFound.size() );
                bool bExpectedFound = listArgument.size() == 2;
                for ( size_t foundIndex = 0; foundIndex < listFound.size(); ++foundIndex )
                {
                    // GUID 참조는 지금 경로로 적는다(색인에 없으면 참조 글 그대로).
                    const optional<AssetGUID> guid = AssetRegistry::parseReferenceText( listFound[foundIndex] );
                    const optional<string>    path = guid.has_value() ? AssetRegistry::resolve( *guid ) : optional<string>{};
                    const string_view         name = path.has_value() ? string_view{ *path } : string_view{ listFound[foundIndex] };
                    bExpectedFound                 = bExpectedFound || name == listArgument[2];
                    if ( foundIndex < kMaxListedMaterial )
                        builder.appendFormat( " %#", name );
                }
                if ( bExpectedFound == false )
                    builder.appendFormat( " - %# is not among them", listArgument[2] );
                outReply = builder.view();
                return bExpectedFound;
            }

            /** @brief `matswitch <머티리얼> <스위치> <0|1>` — 캐시에 든 머티리얼의 런타임 스위치를 바꿉니다. 시나리오가 런타임 퍼뮤테이션 전환(물체가 사라지지 않는다)을 확인할 때 쓴다. */
            static bool runSetStaticSwitch( const vector<string>& listArgument, string& outReply )
            {
                const bool bBadValue = listArgument.size() == 3 && listArgument[2] != "0" && listArgument[2] != "1";
                if ( listArgument.size() != 3 || bBadValue )
                {
                    outReply = "usage: matswitch <material> <switch> <0|1>";
                    return false;
                }
                MaterialCache* const pCache = MaterialCache::findBound();
                if ( pCache == nullptr || pCache->isCached( listArgument[0] ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "material %# is not loaded", listArgument[0] );
                    outReply = builder.view();
                    return false;
                }
                Material* const pMaterial = pCache->acquire( listArgument[0] );
                const bool      bSet      = pMaterial != nullptr && pMaterial->setStaticSwitch( hashed_string( string_view( listArgument[1] ) ), listArgument[2] == "1" );
                pCache->release( listArgument[0] );
                StringBuilder<constant::buffer::kMax256> builder;
                if ( bSet )
                    builder.appendFormat( "set %#", listArgument[1] );
                else
                    builder.appendFormat( "could not set %# of %#", listArgument[1], listArgument[0] );
                outReply = builder.view();
                return bSet;
            }

            /** @brief `matparam <경로> <이름> <x> [y] [z] [w]` — 묶음의 값을 바꿉니다. 시나리오가 런타임 변경을 확인할 때 쓴다. */
            static bool runSetValue( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 || listArgument.size() > 6 )
                {
                    outReply = "usage: matparam <collection> <name> <x> [y] [z] [w]";
                    return false;
                }
                float4 value{};
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    float32 component{ 0.0f };
                    if ( StringUtil::parseFloat( listArgument[index], component ) == false )
                    {
                        StringBuilder<constant::buffer::kMax256> builder;
                        builder.appendFormat( "not a number: %#", listArgument[index] );
                        outReply = builder.view();
                        return false;
                    }
                    ( index == 2 ? value._x : index == 3 ? value._y
                                          : index == 4   ? value._z
                                                         : value._w ) = component;
                }
                const shared_ptr<MaterialParameterCollection> pCollection = MaterialParameterCollection::acquire( AssetID{ listArgument[0] } );
                if ( pCollection == nullptr || pCollection->setValue( hashed_string( string_view( listArgument[1] ) ), value ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "could not set %# of %#", listArgument[1], listArgument[0] );
                    outReply = builder.view();
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( "set %#", listArgument[1] );
                outReply = builder.view();
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MaterialStaticSwitch, "matswitch", "matswitch <material> <switch> <0|1>", "Turn a runtime static switch (bShaderFeature=\"0\") of a loaded material on or off",
                    &MaterialCommandsInternal::runSetStaticSwitch );
    SW_DEV_COMMAND( MaterialFind, "matfind", "matfind <keyword|shader> <value> [material]", "List the materials whose asset state turns a keyword on or that draw with a shader (loaded or not)",
                    &MaterialCommandsInternal::runFind );
    SW_DEV_COMMAND( MaterialParameter, "matparam", "matparam <collection> <name> <x> [y] [z] [w]", "Set a value of a material parameter collection; every material bound to it follows",
                    &MaterialCommandsInternal::runSetValue );
} // namespace sw
