#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

#include <optional>

namespace sw
{
    namespace
    {
        struct FighterCatalogInternal
        {
            /** @brief "Standing,Crouching,Airborne,Stance" 를 비트로 읽습니다. 문자열이 비면 @p inoutMask 를 그대로 둡니다(스키마 초기값). 모르는 낱말은 오류입니다. */
            [[nodiscard]] static bool readPostureMask( string_view text, string_view sourceName, const hashed_string& moveID, uint8& inoutMask )
            {
                if ( text.empty() )
                    return true;
                uint8 mask   = 0;
                bool  bValid = true;
                StringUtil::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    if ( StringUtil::equals( token, "Standing" ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Standing ) ) );
                    else if ( StringUtil::equals( token, "Crouching" ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Crouching ) ) );
                    else if ( StringUtil::equals( token, "Airborne" ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Airborne ) ) );
                    else if ( StringUtil::equals( token, "Stance" ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Stance ) ) );
                    else
                    {
                        SW_LOG_ERROR_ID( "FighterCatalog.MoveUnknownPostureStanding", "%#: move '%#' has an unknown posture '%#' (Standing, Crouching, Airborne, Stance)", sourceName, moveID.c_str(), string( token ) );
                        bValid = false;
                    }
                } );
                if ( bValid )
                    inoutMask = mask;
                return bValid;
            }

            /** @brief 반드시 적는 커맨드 속성 @p pName 을 읽습니다. 없거나 읽지 못하면 오류입니다. */
            [[nodiscard]] static bool readRequiredCommand( const InputCommandParser& parser, const XMLNode& node, const utf8* pName, string_view sourceName,
                                                           InputCommand& outCommand )
            {
                const string_view notation = node.getAttributeText( pName );
                if ( notation.empty() == false && parser.parse( notation, outCommand ) )
                    return true;
                SW_LOG_ERROR_ID( "FighterCatalog.IdNeedsReadableCommand", "%#: <%# id=\"%#\"> needs a readable %#=\"...\" command", sourceName, node.getName(), node.getAttributeText( "id" ), pName );
                return false;
            }

            /** @brief 없어도 되는 정수 속성을 읽습니다. 없으면 @p inoutValue 를 그대로 두고, 깨졌거나 @p minValue 보다 작으면 오류입니다. */
            [[nodiscard]] static bool readOptionalInt( const XMLNode& node, const utf8* pName, int32 minValue, string_view sourceName, int32& inoutValue )
            {
                std::optional<int32> value;
                const bool           bRead = node.readOptionalAttribute( pName, value );
                if ( bRead == false || ( value.has_value() && *value < minValue ) )
                {
                    SW_LOG_ERROR_ID( "FighterCatalog.IdNeedsIntegerMore", "%#: <%# id=\"%#\" %#=\"%#\"> needs an integer of %# or more", sourceName, node.getName(), node.getAttributeText( "id" ), pName,
                                     node.getAttributeText( pName ), minValue );
                    return false;
                }
                if ( value.has_value() )
                    inoutValue = *value;
                return true;
            }

            static void addStance( FighterDef& fighter, const hashed_string& stance )
            {
                if ( stance.empty() || fighter.findStanceIndex( stance ).has_value() )
                    return;
                fighter._listStance.push_back( stance );
            }

            /** @brief `<Move>` 하나를 읽습니다. 오류면 false 이고 @p outMove 를 쓰지 않습니다. */
            [[nodiscard]] static bool readMove( const XMLNode& node, const InputCommandParser& parser, const MoveCatalog& moveCatalog, string_view sourceName,
                                                FighterMove& outMove )
            {
                const utf8* pMoveID = XMLReadUtil::findRequiredID( node, sourceName );
                if ( pMoveID == nullptr )
                    return false;
                FighterMove          move;
                bool                 bValid   = XMLFields::readFields( node, move, sourceName, { "id", "frames", "command", "priority", "maxGap", "from", "breakButtons" } );
                const string_view    framesID = node.getAttributeText( "frames" );
                const MoveFrameData* pFrame   = moveCatalog.find( hashed_string( framesID.empty() ? string_view( pMoveID ) : framesID ) );
                if ( pFrame == nullptr )
                {
                    SW_LOG_ERROR_ID( "FighterCatalog.MoveNoFrameData", "%#: move '%#' has no frame data in the MoveCatalog", sourceName, pMoveID );
                    return false;
                }
                move._frame       = *pFrame;
                move._frame._id   = hashed_string( pMoveID );
                bValid            = readRequiredCommand( parser, node, "command", sourceName, move._command ) && bValid;
                move._command._id = move._frame._id;
                bValid            = readOptionalInt( node, "priority", 0, sourceName, move._command._priority ) && bValid;
                bValid            = readOptionalInt( node, "maxGap", 1, sourceName, move._command._maxGap ) && bValid;
                if ( move._stance.empty() == false )
                    move._postureMask = static_cast<uint8>( 1u << static_cast<uint32>( FighterPosture::Stance ) );
                else
                    bValid = readPostureMask( node.getAttributeText( "from" ), sourceName, move._frame._id, move._postureMask ) && bValid;
                const string_view breakButtons = node.getAttributeText( "breakButtons" );
                if ( breakButtons.empty() == false )
                {
                    InputCommand breakCommand;
                    if ( parser.parse( breakButtons, breakCommand ) && breakCommand._listStep.size() == 1 )
                        move._breakButtons = breakCommand._listStep[0]._buttons;
                    else
                    {
                        SW_LOG_ERROR_ID( "FighterCatalog.MoveUnreadableBreakButtonsOne", "%#: move '%#' has unreadable breakButtons '%#' (one step such as \"1\" or \"1+2\")", sourceName, pMoveID, breakButtons );
                        bValid = false;
                    }
                }
                if ( bValid == false )
                    return false;
                outMove = move;
                return true;
            }
        };
    } // namespace

    // ------------------------------------------------------------------------------
    // FighterDef
    // ------------------------------------------------------------------------------
    optional<int32> FighterDef::findMoveIndex( const hashed_string& moveID ) const
    {
        for ( size_t index = 0; index < _listMove.size(); ++index )
        {
            if ( _listMove[index]._frame._id == moveID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    optional<int32> FighterDef::findStanceIndex( const hashed_string& stance ) const
    {
        for ( size_t index = 0; index < _listStance.size(); ++index )
        {
            if ( _listStance[index] == stance )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    // ------------------------------------------------------------------------------
    // FighterCatalog
    // ------------------------------------------------------------------------------
    FighterCatalog::FighterCatalog()
    {
    }

    bool FighterCatalog::loadFromAsset( const AssetID& assetID )
    {
        return GameDataXML::loadFromAsset( *this, &FighterCatalog::loadFileRoot, assetID, "FighterCatalog" );
    }

    uint32 FighterCatalog::loadFileRoot( const XMLNode& root, string_view sourceName )
    {
        // 기반 기술 카탈로그는 데이터가 정한다 — 공유 캐시는 경로만으로 나눠 주므로 호출하는 쪽의 문맥을 받을 수 없다(작물의 계절과 같은 규칙).
        const string_view movesPath = root.getAttributeText( "moves" );
        if ( movesPath.empty() )
        {
            SW_LOG_ERROR_ID( "FighterCatalog.FileNeedsMoves", "%#: <FighterCatalog> read from a file needs moves=\"<move catalog path>\"", sourceName );
            return 0;
        }
        const shared_ptr<const MoveCatalog> pMoves = SharedAssetCache<MoveCatalog>::acquire( movesPath );
        if ( pMoves == nullptr )
        {
            SW_LOG_ERROR_ID( "FighterCatalog.MovesCouldNot", "%#: the move catalog '%#' could not be read", sourceName, movesPath );
            return 0;
        }
        return loadRoot( root, *pMoves, sourceName ); // 프레임 데이터는 기술마다 복사해 온다 — 기반을 쥐지 않는다
    }

    bool FighterCatalog::loadFromXMLText( string_view xmlText, const MoveCatalog& moveCatalog, string_view sourceName )
    {
        return GameDataXML::loadFromXMLText( *this, &FighterCatalog::loadRoot, moveCatalog, xmlText, sourceName, "FighterCatalog" );
    }

    void FighterCatalog::addFighter( const FighterDef& fighter )
    {
        FighterDef copy = fighter;
        for ( const FighterMove& move : fighter._listMove )
        {
            FighterCatalogInternal::addStance( copy, move._stance );
        }
        for ( const FighterMove& move : fighter._listMove )
        {
            FighterCatalogInternal::addStance( copy, move._enterStance );
        }
        (void)add( copy ); // 빈 id 는 카탈로그가 거른다
    }

    uint32 FighterCatalog::loadRoot( const XMLNode& root, const MoveCatalog& moveCatalog, string_view sourceName )
    {
        InputCommandParser parser;
        const string_view  buttonNames = root.getAttributeText( "buttons" );
        if ( buttonNames.empty() == false )
            parser.setButtonNames( buttonNames );

        /** 표의 줄은 ( node, sourceName ) 만 받으므로 문맥을 든 읽는 쪽을 둔다. 함수 안의 형식이라 이 클래스의 비공개를 볼 수 있다. */
        struct FighterElementReader
        {
            FighterCatalog&           _catalog;
            const MoveCatalog&        _moveCatalog;
            const InputCommandParser& _parser;

            bool loadFighter( const XMLNode& node, string_view sourceName )
            {
                using Internal = FighterCatalogInternal;
                // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
                FighterDef fighter;
                bool       bValid = XMLFields::readFields( node, fighter, sourceName, { "sidestep", "sidestepDown", "jump" } );
                bValid            = Internal::readRequiredCommand( _parser, node, "sidestep", sourceName, fighter._sidestepUp ) && bValid;
                bValid            = Internal::readRequiredCommand( _parser, node, "sidestepDown", sourceName, fighter._sidestepDown ) && bValid;
                bValid            = Internal::readRequiredCommand( _parser, node, "jump", sourceName, fighter._jump ) && bValid;
                if ( bValid == false )
                {
                    SW_LOG_ERROR_ID( "FighterCatalog.FighterIdErrorsNot", "%#: <Fighter id=\"%#\"> has errors - not added", sourceName, fighter._id.c_str() );
                    return false;
                }
                // 틀린 기술은 그 기술만 버린다(오류는 남긴다) — 나머지 기술로 캐릭터를 쓸 수 있다.
                for ( XMLNode child = node.findChild( "Move" ); child; child = child.findNextSibling( "Move" ) )
                {
                    FighterMove move;
                    if ( Internal::readMove( child, _parser, _moveCatalog, sourceName, move ) )
                        fighter._listMove.push_back( move );
                    else
                        SW_LOG_ERROR_ID( "FighterCatalog.FighterMoveNotRead", "%#: fighter '%#' move was not read - skipped", sourceName, fighter._id.c_str() );
                }
                if ( fighter._listMove.empty() )
                    SW_LOG_WARNING_ID( "FighterCatalog.FighterNoUsableMove", "%#: fighter '%#' has no usable <Move>", sourceName, fighter._id.c_str() );
                _catalog.addFighter( fighter );
                return true;
            }
        };
        using Element                          = XMLCatalogElement<FighterElementReader>;
        static constexpr Element kArrElement[] = {
            Element::of<&FighterElementReader::loadFighter>( "Fighter" ),
        };
        FighterElementReader reader{ *this, moveCatalog, parser };
        return XMLCatalogReadResult::readInDocumentOrder( reader, root, sourceName, kArrElement )._loadedCount;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<FighterCatalog>::acquire`). 파일 규칙이 없는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( FighterCatalog, FighterCatalog, SW_KIT_API, AssetTypeDef::makeCacheOnly( "FighterCatalog", AssetTypeLoader::of<FighterCatalog>( MemoryTag::Game ) ) );
} // namespace sw
