#include "pch.h"

#include "Editor/Common/GUI/EditorComponentIcon.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    namespace
    {
        struct EditorComponentIconInternal
        {
            static constexpr Color4 kLight{ 1.00f, 0.83f, 0.30f, 1.0f };
            static constexpr Color4 kCamera{ 0.45f, 0.70f, 1.00f, 1.0f };
            static constexpr Color4 kAudio{ 0.45f, 0.90f, 0.60f, 1.0f };
            static constexpr Color4 kPhysics{ 1.00f, 0.60f, 0.30f, 1.0f };
            static constexpr Color4 kNavigation{ 0.35f, 0.85f, 0.85f, 1.0f };
            static constexpr Color4 kEnvironment{ 0.55f, 0.80f, 0.95f, 1.0f };
            static constexpr Color4 kNeutral{ 0.85f, 0.87f, 0.90f, 1.0f };

            /** @brief 타입 이름 표 — 파생 타입이 기반보다 먼저 맞는다(찾기가 타입 자신부터 부모로 올라간다). */
            static constexpr EditorComponentIconRow kArrTypeRow[] = {
                {               "CameraComponent",           editoricon::kCamera,      kCamera,  true},
                {     "DirectionalLightComponent", editoricon::kLightDirectional,       kLight,  true},
                {           "PointLightComponent",       editoricon::kLightPoint,       kLight,  true},
                {            "SpotLightComponent",        editoricon::kLightSpot,       kLight,  true},
                {                "LightComponent",       editoricon::kLightPoint,       kLight,  true},
                {         "PointLight2DComponent",       editoricon::kLightPoint,       kLight,  true},
                {        "GlobalLight2DComponent",      editoricon::kLightGlobal,       kLight,  true},
                {         "AudioEmitterComponent",     editoricon::kAudioEmitter,       kAudio,  true},
                {  "AudioAmbientEmitterComponent",     editoricon::kAudioEmitter,       kAudio,  true},
                {        "AudioListenerComponent",    editoricon::kAudioListener,       kAudio,  true},
                {      "AudioReverbZoneComponent",        editoricon::kAudioZone,       kAudio,  true},
                {                 "WindComponent",             editoricon::kWind, kEnvironment,  true},
                {                 "MeshComponent",             editoricon::kCube,     kNeutral, false},
                {         "SkeletalMeshComponent",         editoricon::kSkeleton,     kNeutral, false},
                {               "SpriteComponent",           editoricon::kSprite,     kNeutral, false},
                {       "SpriteAnimatorComponent",        editoricon::kAnimation,     kNeutral, false},
                {     "SkeletalAnimatorComponent",        editoricon::kAnimation,     kNeutral, false},
                {      "FacialAnimationComponent",        editoricon::kAnimation,     kNeutral, false},
                {      "TileMapRendererComponent",          editoricon::kTileMap,     kNeutral, false},
                {        "BoxCollider2DComponent",         editoricon::kCollider,     kPhysics, false},
                {            "RigidBodyComponent",          editoricon::kPhysics,     kPhysics, false},
                {          "RigidBody2DComponent",          editoricon::kPhysics,     kPhysics, false},
                {              "PhysicsComponent",          editoricon::kPhysics,     kPhysics, false},
                {  "CharacterControllerComponent",        editoricon::kCharacter,     kNeutral, false},
                {"CharacterController2DComponent",        editoricon::kCharacter,     kNeutral, false},
                {         "NavMeshAgentComponent",       editoricon::kNavigation,  kNavigation, false},
                {      "NavMeshObstacleComponent",       editoricon::kNavigation,  kNavigation, false},
                {       "NavMeshSurfaceComponent",       editoricon::kNavigation,  kNavigation, false},
                {      "NavMeshModifierComponent",       editoricon::kNavigation,  kNavigation, false},
                {            "WaterBodyComponent",            editoricon::kWater, kEnvironment, false},
                {              "FoliageComponent",          editoricon::kFoliage, kEnvironment, false},
                {              "TerrainComponent",      editoricon::kHeightfield, kEnvironment, false},
                {       "WheeledVehicleComponent",          editoricon::kVehicle,     kNeutral, false},
                {             "FractureComponent",         editoricon::kFracture,     kNeutral, false},
                {           "Fracture2DComponent",         editoricon::kFracture,     kNeutral, false},
                {       "SequencePlayerComponent",         editoricon::kSequence,     kNeutral, false},
                {               "WidgetComponent",           editoricon::kWidget,     kNeutral, false},
                {                  "TagComponent",              editoricon::kTag,     kNeutral, false},
                {              "MissingComponent",          editoricon::kMissing,     kNeutral, false},
                {                "SceneComponent",             editoricon::kAxes,     kNeutral, false},
            };

            /** @brief 리플렉션 Category 표 — 타입 이름 표에 없는 컴포넌트(키트 · 게임)가 여기서 맞는다. */
            static constexpr EditorComponentIconRow kArrCategoryRow[] = {
                {      "Camera",     editoricon::kCamera,      kCamera, false},
                {       "Audio",      editoricon::kAudio,       kAudio, false},
                {     "Physics",    editoricon::kPhysics,     kPhysics, false},
                {  "Physics 2D",    editoricon::kPhysics,     kPhysics, false},
                {  "Navigation", editoricon::kNavigation,  kNavigation, false},
                {   "Character",  editoricon::kCharacter,     kNeutral, false},
                {   "Animation",  editoricon::kAnimation,     kNeutral, false},
                {"Animation 2D",  editoricon::kAnimation,     kNeutral, false},
                {"Animation 3D",  editoricon::kAnimation,     kNeutral, false},
                {"Rendering 3D",       editoricon::kCube,     kNeutral, false},
                {"Rendering 2D",     editoricon::kSprite,     kNeutral, false},
                {          "UI",     editoricon::kWidget,     kNeutral, false},
                {      "Layout",     editoricon::kWidget,     kNeutral, false},
                {     "Control", editoricon::kController,     kNeutral, false},
                { "Environment",      editoricon::kGlobe, kEnvironment, false},
                {     "Vehicle",    editoricon::kVehicle,     kNeutral, false},
                { "Interaction",    editoricon::kTrigger,     kNeutral, false},
                {     "Gimmick",    editoricon::kTrigger,     kNeutral, false},
                {  "Cinematics",   editoricon::kSequence,     kNeutral, false},
                {    "Gameplay",    editoricon::kAbility,     kNeutral, false},
            };

            static constexpr EditorComponentIconRow kDefaultRow{ "", editoricon::kComponent, kNeutral, false };
            static constexpr EditorComponentIconRow kObjectRow{ "", editoricon::kGameObject, kNeutral, false };
        };
    } // namespace

    const EditorComponentIconRow& EditorComponentIcon::findRow( const TypeInfo* pType )
    {
        for ( const TypeInfo* pCurrent = pType; pCurrent != nullptr; pCurrent = pCurrent->getParentType() )
        {
            const string_view name{ pCurrent->_name.c_str() };
            for ( const EditorComponentIconRow& row : EditorComponentIconInternal::kArrTypeRow )
            {
                if ( name == row._pKey )
                    return row;
            }
        }
        if ( pType != nullptr && pType->getCategory().empty() == false )
        {
            for ( const EditorComponentIconRow& row : EditorComponentIconInternal::kArrCategoryRow )
            {
                if ( string_view{ pType->getCategory() } == row._pKey )
                    return row;
            }
        }
        return EditorComponentIconInternal::kDefaultRow;
    }

    const EditorComponentIconRow& EditorComponentIcon::findObjectRow( const GameObject& object )
    {
        for ( const Component* pComponent : object.getComponents() )
        {
            if ( pComponent == nullptr )
                continue;
            const EditorComponentIconRow& row = findRow( pComponent->getTypeInfo() );
            if ( row._bBillboard )
                return row;
        }
        return EditorComponentIconInternal::kObjectRow;
    }

    uint32 EditorComponentIcon::getTypeRowCount()
    {
        return static_cast<uint32>( sizeof( EditorComponentIconInternal::kArrTypeRow ) / sizeof( EditorComponentIconInternal::kArrTypeRow[0] ) );
    }

    const EditorComponentIconRow& EditorComponentIcon::getTypeRow( uint32 index )
    {
        return EditorComponentIconInternal::kArrTypeRow[index < getTypeRowCount() ? index : 0];
    }

    uint32 EditorComponentIcon::decodeGlyph( const utf8* pGlyph )
    {
        if ( pGlyph == nullptr )
            return 0;
        const auto* pByte = reinterpret_cast<const uint8*>( pGlyph );
        if ( pByte[0] < 0x80 )
            return pByte[0];
        if ( ( pByte[0] & 0xE0 ) == 0xC0 && pByte[1] != 0 )
            return ( static_cast<uint32>( pByte[0] & 0x1F ) << 6 ) | ( pByte[1] & 0x3F );
        if ( ( pByte[0] & 0xF0 ) == 0xE0 && pByte[1] != 0 && pByte[2] != 0 )
            return ( static_cast<uint32>( pByte[0] & 0x0F ) << 12 ) | ( static_cast<uint32>( pByte[1] & 0x3F ) << 6 ) | ( pByte[2] & 0x3F );
        return 0;
    }
} // namespace sw::editor
