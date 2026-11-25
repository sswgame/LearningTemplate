#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"

namespace sw
{
    namespace
    {
        /** @brief 머티리얼 값을 런타임에 바꾸는 개발 명령(`matswitch` · `matparam`)입니다. */
        struct MaterialCommandsInternal
        {
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
    SW_DEV_COMMAND( MaterialParameter, "matparam", "matparam <collection> <name> <x> [y] [z] [w]", "Set a value of a material parameter collection; every material bound to it follows",
                    &MaterialCommandsInternal::runSetValue );
} // namespace sw
