/**
 * @file MaterialWatches.cpp
 * @brief 머티리얼의 자동화 감시 값입니다. `Material.CollectionValue`(쥔 머티리얼 파라미터 묶음의 값 한 채널).
 * @details 감시 값 층(5)은 그래픽 데이터 층(4)보다 위라 묶음 파일(`MaterialParameterCollection.cpp`)에 두지 못하고, 머티리얼을 쓰는 기능 층(6)에 둔다.
 */
#include "pch.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"

namespace sw
{
    namespace
    {
        struct MaterialWatchesInternal
        {
            static constexpr utf8 kArrChannel[] = { 'x', 'y', 'z', 'w' };

            /** @brief 대상은 묶음 경로, 키는 `<이름>.<x|y|z|w>` 입니다. 쥔 묶음만 읽습니다(파일을 새로 읽지 않는다 — 핫 리로드가 쥔 묶음을 바꿨는지 본다). */
            [[nodiscard]] static bool readCollectionValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const size_t dot = query._key.rfind( '.' );
                if ( dot == string_view::npos || dot + 2 != query._key.size() )
                {
                    outValue.setText( "key must be <parameter>.<x|y|z|w>" );
                    return false;
                }
                int32 channel = -1;
                for ( int32 index = 0; index < 4; ++index )
                {
                    if ( kArrChannel[index] == query._key[dot + 1] )
                        channel = index;
                }
                const shared_ptr<MaterialParameterCollection> pCollection =
                    channel >= 0 ? engine::getSubsystem<AssetManager>().getAssetCache<MaterialParameterCollectionCache>().findLive( AssetID{ query._target } ) : nullptr;
                const float4* pValue = pCollection != nullptr ? pCollection->findValue( hashed_string( query._key.substr( 0, dot ) ) ) : nullptr;
                if ( pValue == nullptr )
                {
                    outValue.setText( "no such channel, no material holds the collection, or no such parameter" );
                    return false;
                }
                const float32 arrComponent[] = { pValue->_x, pValue->_y, pValue->_z, pValue->_w };
                outValue.setNumber( static_cast<float64>( arrComponent[channel] ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( materialCollectionValue, "Material.CollectionValue",
                               "Value channel of a material parameter collection a material holds (target = collection path, key = <parameter>.<x|y|z|w>)",
                               &MaterialWatchesInternal::readCollectionValue, true, true );
} // namespace sw
