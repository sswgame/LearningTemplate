#include "pch.h"

#include "Engine/Audio/AudioEvent.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        /** @brief 이벤트 프리셋(`_parentEvent`)을 `DataInheritance` 의 입력으로 옮깁니다. 이벤트 하나가 가상의 문서이고 부모는 같은 파일의 이벤트입니다. */
        struct AudioEventLibraryInternal
        {
            static constexpr const utf8* kParentEventAttribute = "_parentEvent";
            static constexpr const utf8* kEventListElement     = "_listEvent";
            static constexpr const utf8* kEventNameAttribute   = "_name";

            /**
             * @brief 이벤트 원소를 문서 하나로 씁니다. 부모 표시는 `_base` 로 옮깁니다(파일 상속의 합치기가 목록 원소의 `_base` 를 지우므로 데이터에는 다른 이름으로 적는다).
             * @param bKeepParent false 면 부모 표시를 뺍니다 — 차이를 만들 때 두 이벤트의 칸 값만 맞댄다.
             */
            [[nodiscard]] static string makeEventDocument( const XMLNode& event, bool bKeepParent = true )
            {
                XMLDocument   doc;
                const XMLNode out = doc.appendRoot( event.getName() );
                for ( XMLAttribute attribute = event.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const bool bParent = StringUtil::equals( attribute.getName(), kParentEventAttribute );
                    if ( bParent && ( bKeepParent == false || StringUtil::isNullOrEmpty( attribute.getValue() ) ) )
                        continue; // 빈 부모는 부모가 없는 것이다(직렬화기가 빈 칸도 쓴다)
                    out.setAttribute( bParent ? DataInheritance::kBaseAttribute : attribute.getName(), string_view( attribute.getValue() ) );
                }
                for ( XMLNode child = event.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    (void)out.appendClone( child );
                }
                return doc.saveToString();
            }

            /** @brief 부모 이벤트를 이름으로 찾아 문서로 돌려줍니다. */
            class EventSource final : public IDataInheritanceSource
            {
            public:
                explicit EventSource( const XMLNode& eventList )
                    : _eventList{ eventList }
                {
                }

                [[nodiscard]] bool readText( string_view assetID, string& outText ) override
                {
                    for ( XMLNode event = _eventList.findChild(); event.isValid(); event = event.findNextSibling() )
                    {
                        if ( event.getAttributeText( kEventNameAttribute ) == assetID )
                        {
                            outText = makeEventDocument( event );
                            return true;
                        }
                    }
                    return false;
                }

            private:
                XMLNode _eventList;
            };

            /**
             * @brief `_parentEvent` 가 있는 이벤트를 펼칩니다. 없으면 @p outText 가 비고 글 검색 한 번으로 끝납니다.
             * @details 부모가 같은 글 안에 있어 쿠킹본에서도 그대로 돈다 — 쿠커가 펼치는 것은 파일 `_base` 까지다.
             * @return 없는 부모 · 순환이면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool flattenPresets( string_view text, string_view sourceName, string& outText )
            {
                outText.clear();
                if ( text.find( kParentEventAttribute ) == string_view::npos )
                    return true;
                XMLDocument doc;
                if ( doc.parse( text, sourceName ) == false )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.PresetNotParsed", "%#: %#", sourceName, doc.getLastError() );
                    return false;
                }
                const XMLNode               root      = doc.getRoot();
                const XMLNode               eventList = root.findChild( kEventListElement );
                EventSource                 source{ eventList };
                const DataInheritanceSchema schema = DataInheritanceSchema::makeFromType( *AudioEventDef::StaticType() );

                XMLDocument   outDoc;
                const XMLNode outRoot = outDoc.appendRoot( root.getName() );
                for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    outRoot.setAttribute( attribute.getName(), string_view( attribute.getValue() ) );
                }
                for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), kEventListElement ) == false )
                    {
                        (void)outRoot.appendClone( child );
                        continue;
                    }
                    const XMLNode outList = outRoot.appendChild( kEventListElement );
                    for ( XMLNode event = child.findChild(); event.isValid(); event = event.findNextSibling() )
                    {
                        if ( event.getAttributeText( kParentEventAttribute ).empty() )
                        {
                            (void)outList.appendClone( event );
                            continue;
                        }
                        string entryName{ sourceName };
                        entryName += "#";
                        entryName += event.getAttributeText( kEventNameAttribute );
                        string      mergedText;
                        XMLDocument mergedDoc;
                        if ( DataInheritance::flatten( makeEventDocument( event ), entryName, schema, source, mergedText ) == false || mergedDoc.parse( mergedText, entryName ) == false )
                            return false;
                        // 합치기가 지운 부모 표시를 다시 단다 — 읽은 이벤트가 부모를 알아야 저장이 차이로 쓴다(`writePresetDeltas`).
                        mergedDoc.getRoot().setAttribute( kParentEventAttribute, event.getAttributeText( kParentEventAttribute ) );
                        (void)outList.appendClone( mergedDoc.getRoot() );
                    }
                }
                outText = outDoc.saveToString();
                return true;
            }

            /**
             * @brief `flattenPresets` 의 반대입니다. 펼친 라이브러리 글에서 `_parentEvent` 가 있는 이벤트를 그 부모 이벤트(펼친 값)와 다른 칸만 남긴 원소로 바꿉니다.
             * @return 부모 이벤트가 글에 없으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writePresetDeltas( string_view fullText, string& outText )
            {
                XMLDocument doc;
                if ( doc.parse( fullText, "audio event library" ) == false )
                    return false;
                const XMLNode               root      = doc.getRoot();
                const XMLNode               eventList = root.findChild( kEventListElement );
                const DataInheritanceSchema schema    = DataInheritanceSchema::makeFromType( *AudioEventDef::StaticType() );
                XMLDocument                 outDoc;
                const XMLNode               outRoot = outDoc.appendRoot( root.getName() );
                for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    outRoot.setAttribute( attribute.getName(), string_view( attribute.getValue() ) );
                }
                for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), kEventListElement ) == false )
                    {
                        (void)outRoot.appendClone( child );
                        continue;
                    }
                    const XMLNode outList = outRoot.appendChild( kEventListElement );
                    for ( XMLNode event = child.findChild(); event.isValid(); event = event.findNextSibling() )
                    {
                        const string_view parentName = event.getAttributeText( kParentEventAttribute );
                        if ( parentName.empty() )
                        {
                            (void)outList.appendClone( event );
                            continue;
                        }
                        XMLNode parent = eventList.findChild();
                        while ( parent.isValid() && parent.getAttributeText( kEventNameAttribute ) != parentName )
                        {
                            parent = parent.findNextSibling();
                        }
                        string      deltaText;
                        XMLDocument deltaDoc;
                        if ( parent.isValid() == false )
                        {
                            SW_LOG_ERROR_ID( "AudioEventLibrary.PresetParentMissing", "Audio event '%#': preset parent '%#' is not in the library", event.getAttributeText( kEventNameAttribute ), parentName );
                            return false;
                        }
                        if ( DataInheritance::makeDelta( makeEventDocument( event, false ), makeEventDocument( parent, false ), schema, parentName, deltaText ) == false || deltaDoc.parse( deltaText, parentName ) == false )
                            return false;
                        const XMLNode deltaRoot = deltaDoc.getRoot();
                        const XMLNode outEvent  = outList.appendChild( event.getName() );
                        for ( XMLAttribute attribute = deltaRoot.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                        {
                            const bool bBase = StringUtil::equals( attribute.getName(), DataInheritance::kBaseAttribute );
                            outEvent.setAttribute( bBase ? kParentEventAttribute : attribute.getName(), string_view( attribute.getValue() ) );
                        }
                        outEvent.setAttribute( kEventNameAttribute, event.getAttributeText( kEventNameAttribute ) ); // 이름은 열쇠다 — 부모와 같아도(그럴 수 없지만) 늘 적는다
                        for ( XMLNode element = deltaRoot.findChild(); element.isValid(); element = element.findNextSibling() )
                        {
                            (void)outEvent.appendClone( element );
                        }
                    }
                }
                outText = outDoc.saveToString();
                return true;
            }
        };
    } // namespace

    bool AudioEventLibrary::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AudioEventLibrary.AudioEventLibraryNot", "Audio event library not found: %#", assetPath );
            return false;
        }
        return loadFromXMLText( text, assetPath );
    }

    bool AudioEventLibrary::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = AudioEventLibrary{};
        // 파일 상속을 먼저 합친다 — 프리셋의 부모는 합친 결과에서 찾는다(자식 파일이 부모 파일의 이벤트를 고치고 부모로 삼는다).
        string fileText;
        bool   bComposed = false; // 펼치지 않으면 fileText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xmlText, sourceName, ResourceContext::forType( *StaticType() ), fileText, bComposed ) == false )
            return false;
        const string_view mergedFile = fileText.empty() ? xmlText : string_view( fileText );
        string            presetText;
        if ( AudioEventLibraryInternal::flattenPresets( mergedFile, sourceName, presetText ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), presetText.empty() ? mergedFile : string_view( presetText ) ) == false )
        {
            SW_LOG_ERROR_ID( "AudioEventLibrary.AudioEventLibraryCould", "%#: audio event library could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioEventLibrary::validate( string_view sourceName ) const
    {
        bool bValid = AudioMixerDef::validateAttenuations( _listAttenuation, sourceName );
        for ( size_t parameterIndex = 0; parameterIndex < _listParameter.size(); ++parameterIndex )
        {
            const AudioParameterDef& parameter = _listParameter[parameterIndex];
            if ( parameter._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.ParameterNoName", "%#: parameter %# has no name", sourceName, parameterIndex );
                bValid = false;
            }
            for ( size_t otherIndex = parameterIndex + 1; otherIndex < _listParameter.size(); ++otherIndex )
            {
                if ( _listParameter[otherIndex]._name == parameter._name )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.ParameterDeclaredTwice", "%#: parameter '%#' is declared twice", sourceName, parameter._name.c_str() );
                    bValid = false;
                }
            }
            const bool bRangeOrdered = parameter._minValue <= parameter._defaultValue && parameter._defaultValue <= parameter._maxValue;
            if ( bRangeOrdered == false )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.ParameterNeedsMinDefault", "%#: parameter '%#' needs min <= default <= max", sourceName, parameter._name.c_str() );
                bValid = false;
            }
        }

        for ( size_t eventIndex = 0; eventIndex < _listEvent.size(); ++eventIndex )
        {
            const AudioEventDef& event = _listEvent[eventIndex];
            if ( event._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.EventNoName", "%#: event %# has no name", sourceName, eventIndex );
                bValid = false;
                continue;
            }
            const utf8* pName = event._name.c_str();
            for ( size_t otherIndex = eventIndex + 1; otherIndex < _listEvent.size(); ++otherIndex )
            {
                if ( _listEvent[otherIndex]._name == event._name )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.EventDeclaredTwice", "%#: event '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            if ( event._parentEvent.empty() == false )
            {
                // 프리셋 사슬은 라이브러리 안에서 끝나야 한다 — 저장이 부모 이벤트와의 차이로 쓰고 읽기가 같은 사슬로 펼친다.
                const AudioEventDef* pParent    = findEvent( event._parentEvent );
                size_t               chainCount = 0;
                while ( pParent != nullptr && pParent != &event && pParent->_parentEvent.empty() == false && chainCount <= _listEvent.size() )
                {
                    pParent = findEvent( pParent->_parentEvent );
                    ++chainCount;
                }
                if ( pParent == nullptr || pParent == &event || chainCount > _listEvent.size() )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.EventPresetParentInvalid", "%#: event '%#' has preset parent '%#' that is missing or loops back", sourceName, pName, event._parentEvent.c_str() );
                    bValid = false;
                }
            }
            if ( event._listClip.empty() )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.EventNoClips", "%#: event '%#' has no clips", sourceName, pName );
                bValid = false;
            }
            bool bHasSubtitle = false;
            for ( const AudioClipEntry& clip : event._listClip )
            {
                if ( clip._path.empty() )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.EventClipNoPath", "%#: event '%#' has a clip with no path", sourceName, pName );
                    bValid = false;
                }
                bHasSubtitle = bHasSubtitle || clip._subtitleKey.empty() == false;
            }
            if ( event._subtitleSpeakerKey.empty() == false && bHasSubtitle == false )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.EventNamesSubtitleSpeaker", "%#: event '%#' names a subtitle speaker but no clip has a subtitle key", sourceName, pName );
                bValid = false;
            }
            if ( event._volumeDbMax < event._volumeDbMin || event._pitchMax < event._pitchMin )
            {
                SW_LOG_ERROR_ID( "AudioEventLibrary.EventVolumePitchRange", "%#: event '%#' has a volume or pitch range with max below min", sourceName, pName );
                bValid = false;
            }
            for ( const AudioParameterMapping& mapping : event._listParameterMap )
            {
                if ( findParameter( mapping._parameter ) == nullptr )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.EventMapsUnknownParameter", "%#: event '%#' maps unknown parameter '%#'", sourceName, pName, mapping._parameter.c_str() );
                    bValid = false;
                }
                if ( mapping._listPoint.empty() || AudioMixerDef::isCurveSorted( mapping._listPoint ) == false )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.EventParameterCurveEmpty", "%#: event '%#' parameter curve '%#' is empty or out of order", sourceName, pName, mapping._parameter.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    const AudioEventDef* AudioEventLibrary::findEvent( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioEventDef::_name>( _listEvent, name );
    }

    AudioEventDef* AudioEventLibrary::findEvent( const hashed_string& name )
    {
        return VectorUtil::findBy<&AudioEventDef::_name>( _listEvent, name );
    }

    string AudioEventLibrary::saveToXMLText() const
    {
        const string fullText = XMLSerializer::serialize( this, *StaticType() );
        const bool   bPreset  = VectorUtil::containsIf( _listEvent, []( const AudioEventDef& event )
           { return event._parentEvent.empty() == false; } );
        if ( bPreset == false )
            return fullText;
        string presetText;
        if ( AudioEventLibraryInternal::writePresetDeltas( fullText, presetText ) == false )
            return {}; // 부모 이벤트가 없는 라이브러리다 — 무엇이 없는지는 위가 알렸다
        return presetText;
    }

    bool AudioEventLibrary::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( validate( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "AudioEventLibrary.AudioEventLibraryNot2", "%#: audio event library not saved - it does not pass validation", absPath.str() );
            return false;
        }
        // 변형은 변형으로 저장한다 — 부모와 다른 칸만 쓴다(`ReflectedXMLFile::saveDescAsChild` 와 같은 규칙).
        string existingText;
        string saveText = saveToXMLText();
        if ( saveText.empty() )
            return false; // 프리셋 차이를 쓰지 못했다(`saveToXMLText` 가 알렸다)
        if ( FileUtil::readOptionalTextFile( absPath.str(), existingText ) == OptionalFileRead::Read )
        {
            const string      baseID = DataInheritance::findBasePath( existingText );
            AudioEventLibrary parent;
            if ( baseID.empty() == false )
            {
                string deltaText;
                if ( parent.loadFromAsset( AssetID{ baseID } ) == false || DataInheritance::makeDelta( saveText, parent.saveToXMLText(), DataInheritanceSchema::makeFromType( *StaticType() ), baseID, deltaText ) == false )
                {
                    SW_LOG_ERROR_ID( "AudioEventLibrary.VariantNotSaved", "%#: audio event library not saved - its parent '%#' could not be read", absPath.str(), baseID );
                    return false;
                }
                saveText = std::move( deltaText );
            }
        }
        if ( FileUtil::writeTextFile( absPath.str(), saveText ) == false )
        {
            SW_LOG_ERROR_ID( "AudioEventLibrary.AudioEventLibraryCould2", "%#: audio event library could not be written to '%#'", absPath.str(), absPath.str() );
            return false;
        }
        return true;
    }

    const AudioParameterDef* AudioEventLibrary::findParameter( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioParameterDef::_name>( _listParameter, name );
    }
} // namespace sw

namespace sw
{

    /** @brief 오디오 이벤트 라이브러리 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_ASSET_TYPE( AudioEventLibrary, AudioEventLibrary::kAssetType.withPathRule( AssetPathRule{ {}, {}, ".audioevents.xml" } ).withLoader( AssetTypeLoader::of<AudioEventLibrary>() ) );
} // namespace sw
