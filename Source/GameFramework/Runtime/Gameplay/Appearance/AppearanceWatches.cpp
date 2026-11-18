/**
 * @file AppearanceWatches.cpp
 * @brief 외형의 자동화 감시 값(`Appearance.Preset` · `Appearance.MergedSections` · `Appearance.MergedCharacters` · `Appearance.SlotItem` · `Appearance.SurfaceChannel` ·
 *        꾸미기 시나리오의 `Appearance.Idle` · `AssembleCount` · `PartCount` · `Value` · `SocketOffset` · `SlotPartOffset` · `BakedMeshHits` · `BakedMeshEntries`)입니다.
 * @details 시나리오만 읽는 감시 값이라 컴포넌트 파일이 아니라 이 파일에 둔다(`VehicleWatches` · `PawnWatches` 와 같은 자리). 컴포넌트의 공개 읽기만 쓴다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceMeshPart.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"

namespace sw
{
    namespace
    {
        struct AppearanceWatchesInternal
        {
            /** @brief 감시 값 대상 오브젝트의 외형 컴포넌트입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const CharacterAppearanceComponent* findWatchTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pObject = query.findTargetObject();
                if ( pObject == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                const CharacterAppearanceComponent* pAppearance = pObject->getComponent<CharacterAppearanceComponent>();
                if ( pAppearance == nullptr )
                    outValue.setText( string( "'" ) + pObject->getName().c_str() + "' has no CharacterAppearanceComponent" );
                return pAppearance;
            }

            /** @brief 조립한 외형의 프리셋 id 입니다. 아직 조립하지 않았으면 빈 글입니다. */
            [[nodiscard]] static bool readPreset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setText( pAppearance->getResolvedAppearance()._presetID.c_str() );
                return true;
            }

            /** @brief 합친 메시의 구간 수입니다(몸 구간 + 합친 부품). 합치지 않았으면 0 입니다. */
            [[nodiscard]] static bool readMergedSections( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pAppearance->getMergedSectionCount() ) );
                return true;
            }

            /** @brief 씬에서 부품을 몸 메시 하나로 합친 외형 수입니다. 합친 캐릭터를 재는 부하 시나리오가 합치기 경로를 탔는지 본다. */
            [[nodiscard]] static bool readMergedCharacters( SceneIterationKey key, const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 merged = 0;
                pManager->forEachComponentOfType<CharacterAppearanceComponent>( key, [&merged]( const CharacterAppearanceComponent* pAppearance )
                {
                    if ( pAppearance->getMergedSectionCount() > 0 )
                        ++merged;
                } );
                outValue = static_cast<float64>( merged );
                return true;
            }

            /** @brief 조립한 외형에서 슬롯(@p query 의 key)에 입은 아이템 id 입니다. 빈 슬롯이면 `(empty)` 입니다. */
            [[nodiscard]] static bool readSlotItem( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const hashed_string slot( query._key );
                for ( const ResolvedPart& part : pAppearance->getResolvedAppearance()._listPart )
                {
                    if ( part._owner == slot && part._itemID.empty() == false )
                    {
                        outValue.setText( part._itemID.c_str() );
                        return true;
                    }
                }
                outValue.setText( "(empty)" );
                return true;
            }

            /** @brief 몸 표면 채널(@p query 의 key — `Wet` · `Dirt` …)의 모든 영역 가운데 가장 큰 값입니다(머티리얼 스칼라와 같은 값). 표면 상태가 꺼졌거나 모르는 채널이면 값이 없습니다. */
            [[nodiscard]] static bool readSurfaceChannel( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const hashed_string          channel( query._key );
                const CharacterSurfaceState& state  = pAppearance->getSurfaceState();
                bool                         bKnown = false;
                for ( const SurfaceChannelDef& def : state.getChannels() )
                {
                    bKnown = bKnown || def._name == channel;
                }
                if ( bKnown == false )
                {
                    outValue.setText( pAppearance->hasSurfaceState() ? "no such surface channel" : "the surface state is off (no _surfaceChannelsPath)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( state.computeMaxValue( channel ) ) );
                return true;
            }

            /** @brief 조립했고 예약한 조립도, 기다리는 메시 조립도 없으면 1 입니다 — 바꾼 외형이 다 걸렸다(`isAssemblyIdle`). 소켓 프리팹 로드는 `Assets.Pending` 이 본다. */
            [[nodiscard]] static bool readIdle( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setNumber( pAppearance->isAssemblyIdle() && pAppearance->getAssembleCount() > 0 ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 조립한 횟수(누적)입니다 — 바꾸기가 실제로 다시 조립했는지(`increaseFrom`) 본다. */
            [[nodiscard]] static bool readAssembleCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pAppearance->getAssembleCount() ) );
                return true;
            }

            /** @brief 해석한 외형에서 주인(@p query 의 key — 칸 · 꾸미기 매개변수, 예: `Hair` · `Head`)의 부품 수입니다. 규칙의 숨김(투구가 머리카락을 감춤)이 뺀 부품은 세지 않는다. */
            [[nodiscard]] static bool readPartCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const hashed_string owner( query._key );
                uint32              count = 0;
                for ( const ResolvedPart& part : pAppearance->getResolvedAppearance()._listPart )
                {
                    count += part._owner == owner ? 1u : 0u;
                }
                outValue.setNumber( static_cast<float64>( count ) );
                return true;
            }

            /**
             * @brief 주인의 부품(@p query 의 key — `<주인>.<부품>`, 예: `Outfit.Pauldron` · `Hair.Hair`)을 숨긴 규칙 id 입니다. 보이면 `(visible)`, 해석에 없으면(입지 않음) `(absent)` 입니다.
             * @details 여러 규칙이 같은 부품을 숨기면 우선순위가 가장 높은 규칙이다(`ResolvedAppearance::findHidingRule`) — 하나를 벗으면 남은 규칙으로 바뀌는지 본다.
             */
            [[nodiscard]] static bool readHiddenBy( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const size_t dot = query._key.find( '.' );
                if ( dot == string_view::npos )
                {
                    outValue.setText( "key must be '<owner>.<part>'" );
                    return false;
                }
                const ResolvedAppearance& resolved = pAppearance->getResolvedAppearance();
                const hashed_string       owner( query._key.substr( 0, dot ) );
                const hashed_string       part( query._key.substr( dot + 1 ) );
                const hashed_string       rule = resolved.findHidingRule( owner, part );
                if ( rule.empty() == false )
                    outValue.setText( rule.c_str() );
                else
                    outValue.setText( resolved.findPart( owner, part ) != nullptr ? "(visible)" : "(absent)" );
                return true;
            }

            /** @brief `<이름>.<x|y|z>` 를 이름과 축 번호로 나눕니다. 축이 없으면 false 입니다. 소켓 이름에는 점이 있어(`MainHand.Muzzle`) 마지막 점으로 나눈다. */
            [[nodiscard]] static bool splitAxisKey( string_view key, string_view& outName, uint32& outAxis )
            {
                const size_t dot = key.rfind( '.' );
                if ( dot == string_view::npos || dot + 2 != key.size() )
                    return false;
                const utf8 axis = key[dot + 1];
                outAxis         = axis == 'x' ? 0u : ( axis == 'y' ? 1u : ( axis == 'z' ? 2u : 3u ) );
                outName         = key.substr( 0, dot );
                return outAxis < 3u;
            }

            /** @brief @p position 에서 캐릭터 자리를 뺀 축 성분입니다(월드 축, 미터). 캐릭터가 어디 놓여도 몸 기준의 거리로 견준다. */
            [[nodiscard]] static bool setOffsetAxis( const CharacterAppearanceComponent& appearance, const float3& position, uint32 axis, AutomationWatchValue& outValue )
            {
                const GameObject*     pOwner = appearance.getOwner();
                const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
                if ( pRoot == nullptr )
                {
                    outValue.setText( "the character has no transform" );
                    return false;
                }
                const float3  offset = position - pRoot->getWorldPosition();
                const float32 value  = axis == 0u ? offset._x : ( axis == 1u ? offset._y : offset._z );
                outValue.setNumber( static_cast<float64>( value ) );
                return true;
            }

            /** @brief 소켓(@p query 의 key — `<소켓>.<x|y|z>`, 예: `Helmet.y` · `MainHand.Muzzle.x`)의 월드 자리에서 캐릭터 자리를 뺀 성분입니다(지난 프레임 포즈). 체형이 본을 옮기면 따라 바뀐다. */
            [[nodiscard]] static bool readSocketOffset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                string_view                         name;
                uint32                              axis = 0;
                if ( pAppearance == nullptr )
                    return false;
                if ( splitAxisKey( query._key, name, axis ) == false )
                {
                    outValue.setText( "key must be '<socket>.<x|y|z>'" );
                    return false;
                }
                float4x4 world;
                if ( pAppearance->findSocketWorldTransform( hashed_string( name ), world ) == false )
                {
                    outValue.setText( "no such socket in the assembled appearance" );
                    return false;
                }
                return setOffsetAxis( *pAppearance, world.getTranslation(), axis, outValue );
            }

            /** @brief 칸(@p query 의 key — `<칸>.<x|y|z>`, 예: `MainHand.x`)에 소켓으로 붙인 첫 부품 오브젝트의 월드 자리에서 캐릭터 자리를 뺀 성분입니다. 부품이 소켓을 따라왔는지 본다. */
            [[nodiscard]] static bool readSlotPartOffset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                string_view                         slot;
                uint32                              axis = 0;
                if ( pAppearance == nullptr )
                    return false;
                if ( splitAxisKey( query._key, slot, axis ) == false )
                {
                    outValue.setText( "key must be '<slot>.<x|y|z>'" );
                    return false;
                }
                const GameObjectHandle handle = pAppearance->findSlotPartObject( hashed_string( slot ) );
                const GameObject*      pPart  = query._pManager != nullptr ? query._pManager->resolveGameObject( handle ) : nullptr;
                const SceneComponent*  pRoot  = pPart != nullptr ? pPart->getPrimarySceneComponent() : nullptr;
                if ( pRoot == nullptr )
                {
                    outValue.setText( "the slot has no socket part object" );
                    return false;
                }
                return setOffsetAxis( *pAppearance, pRoot->getWorldPosition(), axis, outValue );
            }

            /**
             * @brief 지금 입히려는 꾸미기 값(@p query 의 key — 매개변수, 색은 `<이름>.<r|g|b|a>`)을 스키마로 맞춘 값입니다. 슬라이더 · 색 성분은 수, 고르기 · 부착은 항목 이름입니다.
             * @details 정하지 않은 매개변수는 스키마 기본값이다(정규화 — 해석기가 쓰는 값과 같다). 저장 · 불러오기 왕복이 값을 지켰는지 본다.
             */
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const AppearanceDatabase* pDatabase = pAppearance->findDatabase();
                if ( pDatabase == nullptr )
                {
                    outValue.setText( "no AppearanceDatabase" );
                    return false;
                }
                AppearanceSelection selection;
                pAppearance->captureSelection( selection );
                const CustomizationSchemaDef* pSchema = pDatabase->getSchemas().findSchema( selection._schema );
                string_view                   name    = query._key;
                uint32                        channel = 4u; // 색 성분을 적지 않았다
                const size_t                  dot     = name.rfind( '.' );
                if ( dot != string_view::npos && dot + 2 == name.size() )
                {
                    const utf8 suffix = name[dot + 1];
                    channel           = suffix == 'r' ? 0u : ( suffix == 'g' ? 1u : ( suffix == 'b' ? 2u : ( suffix == 'a' ? 3u : 4u ) ) );
                    name              = channel < 4u ? name.substr( 0, dot ) : name;
                }
                const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( hashed_string( name ) ) : nullptr;
                if ( pParam == nullptr )
                {
                    outValue.setText( "the appearance schema has no such parameter" );
                    return false;
                }
                CustomizationValueSet normalized;
                (void)pSchema->normalize( selection._customization, normalized, nullptr ); // 더 새 판이면 기본값 — 감시 값은 그대로 읽는다
                const CustomizationValue* pValue = normalized.findValue( pParam->_name );
                if ( pValue == nullptr )
                {
                    outValue.setText( "the parameter is off (its condition does not hold)" );
                    return false;
                }
                if ( pParam->_kind == CustomizationKind::Choice || pParam->_kind == CustomizationKind::Attachment )
                {
                    outValue.setText( pValue->_option.c_str() );
                    return true;
                }
                const bool bColor = pParam->_kind == CustomizationKind::Color;
                if ( bColor != ( channel < 4u ) )
                {
                    outValue.setText( bColor ? "a colour needs a channel key '<name>.<r|g|b|a>'" : "only a colour takes a channel" );
                    return false;
                }
                const float4& number = pValue->_number;
                const float32 value  = channel == 1u ? number._y : ( channel == 2u ? number._z : ( channel == 3u ? number._w : number._x ) );
                outValue.setNumber( static_cast<float64>( value ) );
                return true;
            }

            /**
             * @brief 칸(@p query 의 key — `<칸>.<매개변수>`, 예: `Head._color`)에 소켓으로 붙인 첫 부품의 머티리얼 인스턴스가 지금 가진 그 매개변수의 글입니다(덮어쓰지 않았으면 머티리얼의 값).
             * @details 염색이 실제 부품 인스턴스에 닿았는지 본다 — 같은 아이템을 입은 두 캐릭터 · 풀에서 다시 꺼낸 부품이 앞 인스턴스의 색을 남기지 않는지(`sameAs` · `differsFrom`).
             */
            [[nodiscard]] static bool readPartMaterial( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const size_t dot = query._key.find( '.' );
                if ( dot == string_view::npos )
                {
                    outValue.setText( "key must be '<slot>.<material parameter>'" );
                    return false;
                }
                const GameObjectHandle  handle    = pAppearance->findSlotPartObject( hashed_string( query._key.substr( 0, dot ) ) );
                const GameObject*       pPart     = query._pManager != nullptr ? query._pManager->resolveGameObject( handle ) : nullptr;
                const MeshComponent*    pMesh     = pPart != nullptr ? pPart->getComponent<MeshComponent>() : nullptr;
                const MaterialInstance* pInstance = pMesh != nullptr ? pMesh->getMaterialInstance().get() : nullptr;
                string                  text;
                if ( pInstance == nullptr || pInstance->getParameter( hashed_string( query._key.substr( dot + 1 ) ), text ) == false )
                {
                    outValue.setText( "the slot has no part with a material instance holding that parameter" );
                    return false;
                }
                outValue.setText( text.c_str() );
                return true;
            }

            /** @brief 구운 · 합친 메시 캐시가 내준 찾기 수(누적)입니다 — 같은 차림으로 되돌리면 오른다. */
            [[nodiscard]] static bool readBakedMeshHits( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager; // 캐시는 씬이 아니라 GameFramework 모듈 서브시스템이 쥔다
                outValue = static_cast<float64>( AppearanceBakedMeshCache::readCounters()._hitCount );
                return true;
            }
            /** @brief 캐시가 지금 쥔 차림 수입니다 — 교체를 반복해도 차림 수만큼에서 멈춰야 한다(새지 않음). */
            [[nodiscard]] static bool readBakedMeshEntries( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                outValue = static_cast<float64>( AppearanceBakedMeshCache::readCounters()._entryCount );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( appearancePreset, "Appearance.Preset", "Preset id the object's CharacterAppearanceComponent last assembled",
                               &AppearanceWatchesInternal::readPreset, true, false );
    SW_AUTOMATION_QUERY_WATCH( appearanceMergedSections, "Appearance.MergedSections",
                               "Sections of the merged body mesh (the body's own plus one per merged part); 0 when the parts are not merged",
                               &AppearanceWatchesInternal::readMergedSections, true, false );
    SW_AUTOMATION_SCENE_WATCH( appearanceMergedCharacters, "Appearance.MergedCharacters", "Appearances in the scene whose parts are merged into the body mesh",
                               &AppearanceWatchesInternal::readMergedCharacters );
    SW_AUTOMATION_QUERY_WATCH( appearanceSlotItem, "Appearance.SlotItem", "Item the assembled appearance wears in the slot named by key ('(empty)' when the slot is empty)",
                               &AppearanceWatchesInternal::readSlotItem, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearanceIdle, "Appearance.Idle", "1 when the appearance has assembled and no assembly or mesh build is scheduled or in flight",
                               &AppearanceWatchesInternal::readIdle, true, false );
    SW_AUTOMATION_QUERY_WATCH( appearanceAssembleCount, "Appearance.AssembleCount", "Times the appearance assembled (cumulative)", &AppearanceWatchesInternal::readAssembleCount, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( appearancePartCount, "Appearance.PartCount", "Resolved parts whose owner (slot or customization parameter) is key - parts a rule hid are not counted",
                               &AppearanceWatchesInternal::readPartCount, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearanceHiddenBy, "Appearance.HiddenBy", "Rule that hid the part named by key '<owner>.<part>' ('(visible)' when shown, '(absent)' when not worn)",
                               &AppearanceWatchesInternal::readHiddenBy, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearanceValue, "Appearance.Value", "Normalized customization value named key (a colour takes '<name>.<r|g|b|a>'); options read as text",
                               &AppearanceWatchesInternal::readValue, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearanceSocketOffset, "Appearance.SocketOffset", "World offset from the character to the socket named by key '<socket>.<x|y|z>' (last frame's pose)",
                               &AppearanceWatchesInternal::readSocketOffset, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearanceSlotPartOffset, "Appearance.SlotPartOffset", "World offset from the character to the first socket part of the slot named by key '<slot>.<x|y|z>'",
                               &AppearanceWatchesInternal::readSlotPartOffset, true, true );
    SW_AUTOMATION_QUERY_WATCH( appearancePartMaterial, "Appearance.PartMaterial", "Material instance parameter of the slot's first socket part, key '<slot>.<parameter>' (as text)",
                               &AppearanceWatchesInternal::readPartMaterial, true, true );
    SW_AUTOMATION_WATCH( appearanceBakedMeshHits, "Appearance.BakedMeshHits", "Baked or merged appearance mesh lookups the cache served (cumulative)",
                         &AppearanceWatchesInternal::readBakedMeshHits );
    SW_AUTOMATION_WATCH( appearanceBakedMeshEntries, "Appearance.BakedMeshEntries", "Outfits the baked appearance mesh cache holds now", &AppearanceWatchesInternal::readBakedMeshEntries );
    SW_AUTOMATION_QUERY_WATCH( appearanceSurfaceChannel, "Appearance.SurfaceChannel", "Largest value of the body surface channel named key (Wet, Dirt, Blood...) over all regions",
                               &AppearanceWatchesInternal::readSurfaceChannel, true, true );
} // namespace sw
