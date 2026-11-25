#include "pch.h"

#include "Programs/ReflectionParser/AstVisitor.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/AnnotationLookup.h"
#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/DeclarationValidator.h"
#include "Programs/ReflectionParser/MemberCollector.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    AstVisitor::AstVisitor( CXTranslationUnit translationUnit, const ParserSession& session, const vector<string>& listTargetFile )
        : _translationUnit{ translationUnit }
        , _pSession{ &session }
        , _listTargetPath{ listTargetFile }
        , _listTargetFile{}
        , _listHeader{}
        , _pLastFile{ nullptr }
        , _lastTargetIndex{ kNoTarget }
        , _pendingMarker{}
        , _bHasError{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( _listTargetPath.empty() )
            _listTargetPath.push_back( CursorReader::takeString( clang_getTranslationUnitSpelling( translationUnit ) ) );

        _listTargetFile.reserve( _listTargetPath.size() );
        for ( const string& path : _listTargetPath )
        {
            _listTargetFile.push_back( clang_getFile( translationUnit, path.c_str() ) );
        }
        _listHeader.resize( _listTargetPath.size() );
    }

    // ------------------------------------------------------------------------------
    // G) AstVisitor — visit / onStructDeclaration / onEnumDeclaration
    // ------------------------------------------------------------------------------
    bool AstVisitor::visit()
    {
        // 대상 헤더를 번역 단위에서 못 찾으면 그 헤더의 산출물은 조용히 비게 된다. 그 자리에서 멈춘다.
        for ( uint32 index = 0; index < _listTargetFile.size(); ++index )
        {
            if ( _listTargetFile[index] == nullptr )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORNotPartParsed", "ERROR: '%#' is not part of the parsed translation unit.", _listTargetPath[index] );
                _bHasError = SW_TRUE;
            }
        }
        if ( _bHasError == SW_TRUE )
            return false;

        clang_visitChildren( clang_getTranslationUnitCursor( _translationUnit ), visitCursor, this );
        static_cast<void>( takePendingMarker( clang_getNullCursor() ) ); // 번역 단위 끝에 남은 표식은 짝이 없다
        for ( uint32 index = 0; index < _listTargetFile.size(); ++index )
        {
            if ( validateConditionalReflectMacros( _listTargetFile[index], _listTargetPath[index] ) == false )
                markHeaderError( _listHeader[index] );
        }
        return _bHasError == SW_FALSE;
    }

    bool AstVisitor::startsWithReflectMacro( const string_view line )
    {
        // 줄 머리에 오는 매크로 전부다(ENUMERATOR 는 열거자 이름 뒤에 온다). `REFLECT_BODY` 도 — 구성마다 StaticType() 이 있다 없다 한다.
        constexpr const utf8* kArrMacroName[] = { annotation::kReflectMacro, annotation::kEnumMacro, annotation::kPropertyMacro,
                                                  annotation::kFunctionMacro, annotation::kReflectContainerMacro, annotation::kReflectBodyPrefix };
        for ( const utf8* pMacroName : kArrMacroName )
        {
            if ( StringUtil::startsWith( line, pMacroName ) == false )
                continue;
            const string_view rest = StringUtil::trim( line.substr( string_view( pMacroName ).size() ) );
            if ( StringUtil::startsWith( rest, "(" ) )
                return true;
        }
        return false;
    }

    bool AstVisitor::validateConditionalReflectMacros( CXFile file, const string& path ) const
    {
        // 파서는 한 구성(정의 한 벌)만 본다 — `#if` 아래의 REFLECT · ENUM · PROPERTY · FUNCTION · REFLECT_CONTAINER · REFLECT_BODY 는 그 구성에서 꺼져 있으면 오류 없이 빠지고, 켜져 있으면
        // 구성마다 레이아웃이 달라진다. 조건부 멤버는 리플렉션 밖에 두고, 리플렉션 멤버는 조건 없이 선언한다. 지금 트리에는 0 건이다.
        size_t            size  = 0;
        const char* const pText = clang_getFileContents( _translationUnit, file, &size );
        if ( pText == nullptr )
            return true;
        const string_view text{ pText, size };
        uint32            depth   = 0;
        uint32            lineNo  = 0;
        bool              bPassed = true;
        size_t            start   = 0;
        while ( start < text.size() )
        {
            const size_t      end  = text.find( '\n', start );
            const string_view line = StringUtil::trim( text.substr( start, end == string_view::npos ? string_view::npos : end - start ) );
            start                  = end == string_view::npos ? text.size() : end + 1;
            ++lineNo;
            if ( StringUtil::startsWith( line, "#" ) )
            {
                const string_view directive = StringUtil::trim( line.substr( 1 ) );
                if ( StringUtil::startsWith( directive, "if" ) )
                    ++depth;
                else if ( StringUtil::startsWith( directive, "endif" ) && depth > 0 )
                    --depth;
                continue;
            }
            if ( depth > 0 && startsWithReflectMacro( line ) )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORReflectMacroUnderIf", "ERROR: %#(%#): a reflection macro is under #if - the parser sees one configuration, so it would be "
                                                                        "silently dropped or change the layout per configuration. Declare reflected members unconditionally.",
                                 path, lineNo );
                bPassed = false;
            }
        }
        return bPassed;
    }

    /**
     * @details 오류는 헤더 단위로 남긴다. 순회 전체를 멈추면(`CXChildVisit_Break`) 헤더 여럿을 한 TU 로 묶을 때 한 헤더의 오타가
     *          나머지 헤더의 수집까지 막는다. 그 헤더만 실패로 두고 계속 돌아, 오류도 헤더마다 다 나온다.
     */
    void AstVisitor::markHeaderError( ParsedHeader& header )
    {
        header._bHasError = SW_TRUE;
        _bHasError        = SW_TRUE;
    }

    /**
     * @details 매크로가 만든 선언은 매크로를 쓴 자리(전개 위치)의 파일로 셉니다. `clang_Location_isFromMainFile` 로 물으면 매크로
     *          위치를 통째로 밖으로 쳐서 `REFLECT_BODY()` 가 만드는 마커 함수가 검사되지 않고, "REFLECT_BODY() 인데 REFLECT() 가 없다"
     *          검사가 죽습니다. 여러 헤더를 한 TU 로 묶을 때도 같은 질문이면 됩니다.
     */
    int32 AstVisitor::findTargetIndex( const CXCursor cursor ) const
    {
        CXFile file = nullptr;
        clang_getExpansionLocation( clang_getCursorLocation( cursor ), &file, nullptr, nullptr, nullptr );
        if ( file == nullptr )
            return kNoTarget;
        if ( file == _pLastFile )
            return _lastTargetIndex;

        int32 targetIndex = kNoTarget;
        for ( uint32 index = 0; index < _listTargetFile.size(); ++index )
        {
            if ( clang_File_isEqual( file, _listTargetFile[index] ) != 0 )
            {
                targetIndex = static_cast<int32>( index );
                break;
            }
        }
        _pLastFile       = file;
        _lastTargetIndex = targetIndex;
        return targetIndex;
    }

    CXChildVisitResult AstVisitor::visitCursor( CXCursor cursor, CXCursor parent, CXClientData clientData )
    {
        AstVisitor*        self = static_cast<AstVisitor*>( clientData );
        const CXCursorKind kind = clang_getCursorKind( cursor );

        // 표식(`REFLECT(…)` 의 static_assert) 다음에 방문하는 커서는 그 바로 다음 형제다 — 표식의 자식으로는 내려가지 않는다.
        const PendingMarker marker = self->takePendingMarker( parent );

        if ( kind == CXCursor_Namespace )
        {
            self->reportUnpairedMarker( marker, cursor );
            return CXChildVisit_Recurse;
        }

        // include 된 외부 · 시스템 헤더의 선언은 바로 건너뛴다(AST 순회 비용을 크게 줄인다)
        const int32 targetIndex = self->findTargetIndex( cursor );
        if ( targetIndex == kNoTarget )
        {
            self->reportUnpairedMarker( marker, cursor );
            return CXChildVisit_Continue;
        }

        ParsedHeader& header = self->_listHeader[static_cast<size_t>( targetIndex )];

        if ( kind == CXCursor_StaticAssert )
        {
            self->reportUnpairedMarker( marker, cursor );
            self->beginMarker( cursor, parent, targetIndex );
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_FieldDecl )
        {
            self->reportUnpairedMarker( marker, cursor );
            const bool bOrphanProperty = AnnotationLookup::findAnnotateAttr( cursor, annotation::kPropertyPrefix ).empty() == false &&
                                         DeclarationValidator::isInsideReflectType( cursor, annotation::kPropertyMacro ) == false;
            if ( bOrphanProperty )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_CXXMethod )
        {
            self->reportUnpairedMarker( marker, cursor );
            // REFLECT_BODY 표식 함수는 형식 쪽에서 본다 — REFLECT 와 서로 필수다(`onRecordDeclaration` · `onStructDeclaration`).
            const bool  bHasFunction = AnnotationLookup::findAnnotateAttr( cursor, annotation::kFunctionPrefix ).empty() == false;
            const bool  bHasProperty = AnnotationLookup::findAnnotateAttr( cursor, annotation::kPropertyPrefix ).empty() == false;
            const utf8* pMacroName   = bHasFunction ? annotation::kFunctionMacro : annotation::kPropertyMacro;
            const bool  bOrphanMacro = ( bHasFunction || bHasProperty ) && DeclarationValidator::isInsideReflectType( cursor, pMacroName ) == false;
            if ( bOrphanMacro )
                self->markHeaderError( header );
            return CXChildVisit_Continue;
        }

        const bool bReflectMarker   = marker._pDesc == &annotation::kReflect;
        const bool bEnumMarker      = marker._pDesc == &annotation::kEnum;
        const bool bContainerMarker = marker._pDesc == &annotation::kReflectContainer;

        // 클래스 템플릿은 FQN 에 인자가 없어 offsetof / TypeRegistrar<T> 가 성립하지 않는다. 경고로 건너뛰면 적은 REFLECT 가 조용히 사라진다 — 생성 오류다.
        // REFLECT_CONTAINER 는 클래스 템플릿에 적는다(칸 형식에서 `ContainerTreeBuilder` 가 읽는다).
        if ( kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization )
        {
            if ( bReflectMarker )
            {
                SW_LOG_ERROR_ID( "AstVisitor.REFLECTClassTemplateNot", "ERROR: REFLECT on a class template is not supported: %# - reflect a concrete type",
                                 CursorReader::makeFullyQualifiedName( cursor ) );
                self->markHeaderError( header );
            }
            else if ( bContainerMarker )
                self->validateContainerMarker( marker, cursor );
            else
                self->reportUnpairedMarker( marker, cursor );
            return CXChildVisit_Continue;
        }

        if ( kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl )
        {
            if ( bEnumMarker )
                self->reportUnpairedMarker( marker, cursor );
            else if ( bContainerMarker )
                self->validateContainerMarker( marker, cursor );
            self->onRecordDeclaration( cursor, bReflectMarker ? &marker._spelling : nullptr, header );
            return CXChildVisit_Recurse;
        }

        if ( kind == CXCursor_EnumDecl && bEnumMarker )
        {
            self->onEnumDeclaration( cursor, marker._spelling, header );
            return CXChildVisit_Continue;
        }
        self->reportUnpairedMarker( marker, cursor );
        return CXChildVisit_Continue;
    }

    void AstVisitor::beginMarker( const CXCursor cursor, const CXCursor parent, const int32 targetIndex )
    {
        for ( const ReflectAnnotationDesc* pDesc : { &annotation::kReflect, &annotation::kEnum, &annotation::kReflectContainer } )
        {
            string spelling = AnnotationLookup::readMarker( cursor, *pDesc );
            if ( spelling.empty() )
                continue;
            _pendingMarker = PendingMarker{ cursor, parent, std::move( spelling ), pDesc, targetIndex };
            return;
        }
    }

    AstVisitor::PendingMarker AstVisitor::takePendingMarker( const CXCursor parent )
    {
        PendingMarker marker = std::move( _pendingMarker );
        _pendingMarker       = PendingMarker{};
        if ( marker._pDesc == nullptr || clang_equalCursors( parent, marker._parent ) != 0 )
            return marker;
        // 다음 커서의 부모가 다르다 — 표식이 선언 없이 범위(이름공간 · 형식 · 헤더)를 닫았다. 조용히 버리면 적은 REFLECT · ENUM 이 사라진다.
        reportMarker( marker, "ends its scope without a declaration after it" );
        return PendingMarker{};
    }

    void AstVisitor::reportUnpairedMarker( const PendingMarker& marker, const CXCursor next )
    {
        if ( marker._pDesc == nullptr )
            return;
        StringBuilder<constant::buffer::kMax512> reason;
        reason.appendFormat( "is followed by '%#' (%#), not the declaration it marks", CursorReader::getCursorSpelling( next ),
                             CursorReader::takeString( clang_getCursorKindSpelling( clang_getCursorKind( next ) ) ) );
        reportMarker( marker, reason.view() );
    }

    void AstVisitor::reportMarker( const PendingMarker& marker, const string_view reason )
    {
        CXFile file = nullptr;
        uint32 line = 0;
        clang_getExpansionLocation( clang_getCursorLocation( marker._cursor ), &file, &line, nullptr, nullptr );
        SW_LOG_ERROR_ID( "AstVisitor.ERRORMarkerNotBeforeType", "ERROR: %#(%#): %#() %# - write it right before the %# it marks.", CursorReader::takeString( clang_getFileName( file ) ), line,
                         marker._pDesc->_pMacroName, reason, marker._pDesc == &annotation::kEnum ? "enum" : "class or struct" );
        markHeaderError( _listHeader[static_cast<size_t>( marker._targetIndex )] );
    }

    void AstVisitor::validateContainerMarker( const PendingMarker& marker, const CXCursor declaration )
    {
        // 종류가 틀린 REFLECT_CONTAINER 는 칸에서 읽을 때 "컨테이너가 아님" 으로 조용히 바뀐다 — 선언한 헤더에서 한 번 멈춘다.
        const vector<string> listToken = AnnotationApply::splitAnnotationArgs( AnnotationApply::annotationArgumentText( marker._spelling, annotation::kReflectContainerPrefix ) );
        ContainerKind        kind      = ContainerKind::None;
        const bool           bValid    = listToken.empty() == false && listToken.size() <= 2 && tryParseContainerKind( listToken[0], kind ) && kind != ContainerKind::None;
        if ( bValid )
            return;
        StringBuilder<constant::buffer::kMax256> reason;
        reason.appendFormat( "on '%#' needs a container kind (Sequence or Map) and at most a wrapper name - got '%#'", CursorReader::makeFullyQualifiedName( declaration ),
                             AnnotationApply::annotationArgumentText( marker._spelling, annotation::kReflectContainerPrefix ) );
        reportMarker( marker, reason.view() );
    }

    void AstVisitor::onRecordDeclaration( const CXCursor cursor, const string* pReflectSpelling, ParsedHeader& outHeader )
    {
        if ( pReflectSpelling != nullptr )
        {
            onStructDeclaration( cursor, *pReflectSpelling, outHeader );
            return;
        }
        // REFLECT_BODY() 만 있고 REFLECT() 가 없으면 등록기가 없어 StaticType() 이 링크에서야 멈춘다. 둘은 서로 필수다(반대쪽은 `onStructDeclaration`).
        // 정의만 본다 — 전방 선언(`class GameObject;`)은 다른 헤더의 정의를 따라가 본문 표식을 찾으므로, 표식이 그 정의 앞에 있어도 여기서 잘못 멈춘다.
        if ( clang_isCursorDefinition( cursor ) == 0 || AnnotationLookup::isReflectedRecord( cursor ) == false )
            return;
        SW_LOG_ERROR_ID( "AstVisitor.ERRORUsedClassStruct", "ERROR: %#() is used in class/struct '%#', but it lacks REFLECT()!", annotation::kReflectBodyPrefix,
                         CursorReader::makeFullyQualifiedName( cursor ) );
        markHeaderError( outHeader );
    }

    bool AstVisitor::resolveHiddenNested( const CXCursor cursor, const string& fqn, bool& outHidden ) const
    {
        outHidden   = false;
        bool bValid = true;
        for ( CXCursor current = cursor;; )
        {
            const CXCursor     parent     = clang_getCursorSemanticParent( current );
            const CXCursorKind parentKind = clang_getCursorKind( parent );
            const bool         bInType    = parentKind == CXCursor_ClassDecl || parentKind == CXCursor_StructDecl || parentKind == CXCursor_UnionDecl || parentKind == CXCursor_ClassTemplate;
            if ( bInType == false )
                break;
            if ( clang_getCXXAccessSpecifier( current ) != CX_CXXPublic )
            {
                outHidden = true;
                // 숨은 이름은 그 바깥 형식의 `REFLECT_BODY()` friend 로만 닿는다(언리얼은 중첩 UCLASS · USTRUCT 자체를 막는다 — 여기서는 바깥이 REFLECT 면 허용한다).
                const bool bReflectedOwner = parentKind != CXCursor_ClassTemplate && AnnotationLookup::isReflectedRecord( parent );
                if ( bReflectedOwner == false )
                {
                    SW_LOG_ERROR_ID( "AstVisitor.ERRORHiddenNestedOwnerNotReflected",
                                     "ERROR: '%#' is a private or protected nested type, but its owner '%#' is not REFLECT - the generated registrar reaches hidden names only through the owner's REFLECT_BODY(). "
                                     "Add REFLECT() and REFLECT_BODY() to the owner or make the nested type public.",
                                     fqn, CursorReader::makeFullyQualifiedName( parent ) );
                    bValid = false;
                }
            }
            current = parent;
        }
        return bValid;
    }

    /**
     * @brief `REFLECT(...)` 매크로가 붙은 struct/class 선언을 파싱합니다.
     *
     * 수집 단계:
     * 1. 클래스 이름과 네임스페이스를 포함한 전체 경로(FQN)
     * 2. `REFLECT(...)` 매크로 인자(Abstract, Static, Category, Alias 등)
     * 3. 부모 클래스를 찾아 상속 관계 연결
     * 4. `REFLECT_BODY()` 매크로가 있는지 확인
     * 5. 멤버 변수(`PROPERTY`)와 멤버 함수(`FUNCTION`) 메타데이터 수집
     */
    void AstVisitor::onStructDeclaration( const CXCursor cursor, const string& spelling, ParsedHeader& outHeader )
    {
        ParsedTypeInfo typeInfo;
        typeInfo._name               = CursorReader::getCursorSpelling( cursor );
        typeInfo._fullyQualifiedName = CursorReader::makeFullyQualifiedName( cursor );
        bool bHidden                 = false;
        if ( resolveHiddenNested( cursor, typeInfo._fullyQualifiedName, bHidden ) == false )
        {
            markHeaderError( outHeader );
            return;
        }
        typeInfo._bHidden = bHidden ? SW_TRUE : SW_FALSE;

        BLOCK( "Parse REFLECT Annotation" )
        {
            if ( AnnotationLookup::applyAnnotation( spelling, typeInfo, typeInfo._fullyQualifiedName ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
            // 순수 가상 함수가 있는 추상 클래스이면 UCLASS(Abstract) 처럼 Abstract 플래그를 켠다
            if ( clang_CXXRecord_isAbstract( cursor ) != 0 )
                typeInfo._bAbstract = SW_TRUE;
        }

        BLOCK( "Collect Bases / Markers / Fields / Methods" )
        {
            MemberCollector collector( typeInfo, *_pSession );
            collector.collect( cursor );
            // 메서드는 프로퍼티 뒤에 선언될 수 있다 — 이름으로 가리키는 애노테이션은 멤버를 다 모은 뒤에 대조한다.
            collector.validate( cursor );
            if ( collector.hasError() )
            {
                markHeaderError( outHeader );
                return;
            }
            collector.synthesizeDefaultConstructor();
            // 추상 타입(REFLECT(Abstract) · C++ 추상)은 팩토리를 내지 않는다 — 팩토리는 `addComponent<T>()` 로 T 를 만든다. 공통 기반
            // 컴포넌트(`LightComponent`)가 그 예다. `NotPlaceable` 도 내지 않는다 — 데이터로 놓는 길을 막고 코드의 정본 생성 길만 남긴다(`RemoteControllerComponent`).
            const bool bRequiresFactory = MemberCollector::isDerivedFromComponent( cursor, _pSession->_config );
            const bool bFactory         = bRequiresFactory && typeInfo._bAbstract == SW_FALSE && typeInfo._bNotPlaceable == SW_FALSE;
            typeInfo._bReflectBody      = collector.isBodyFound() ? SW_TRUE : SW_FALSE;
            typeInfo._bComponentFactory = bFactory ? SW_TRUE : SW_FALSE;
            typeInfo._bComponent        = bRequiresFactory ? SW_TRUE : SW_FALSE;
            // 틱 모드는 컴포넌트 틱의 선언이다. 다른 타입에 적으면 아무 데도 닿지 않는 선언이 조용히 남는다.
            if ( typeInfo._tickMode.empty() == false && bRequiresFactory == false )
            {
                SW_LOG_ERROR_ID( "AstVisitor.ERRORTickModeNotComponent", "ERROR: REFLECT( TickMode = %# ) on '%#', which is not a component - only component ticks have a lane.",
                                 typeInfo._tickMode, typeInfo._fullyQualifiedName );
                markHeaderError( outHeader );
                return;
            }
        }

        // 컴포넌트 등록(틱 · 물리 훅 · 팩토리 이름)은 이름공간 범위 줄이 타입 이름을 쓴다 — 숨은 중첩 컴포넌트는 그 줄을 낼 수 없다. 씬 · 프리팹이 이름으로 찾는 타입이라 숨길 까닭도 없다.
        if ( typeInfo._bHidden == SW_TRUE && typeInfo._bComponent == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORHiddenNestedComponent", "ERROR: component '%#' is a private or protected nested type - components are found by name (scenes, prefabs), make it public.",
                             typeInfo._fullyQualifiedName );
            markHeaderError( outHeader );
            return;
        }

        // REFLECT() 를 붙였으면 REFLECT_BODY() 도 있어야 한다. 없으면 그 타입만 StaticType() 이
        // 없어서, 다른 타입은 `T::StaticType()` 으로 되는 일을 그 타입만 레지스트리 이름 조회로
        // 우회해야 한다. 쓰는 쪽이 타입마다 접근 방법을 외워야 하는 상태가 된다. 경고로 두면
        // 그냥 지나치게 되므로 생성 자체를 실패시킨다.
        if ( typeInfo._bReflectBody == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORREFLECTUsedClass", "ERROR: REFLECT() is used in class/struct '%#', but it lacks REFLECT_BODY()! "
                                                                 "Add REFLECT_BODY(); as the first line of the type body "
                                                                 "(and include \"Engine/Reflection/ReflectionMacros.h\").",
                             typeInfo._fullyQualifiedName.c_str() );
            markHeaderError( outHeader );
            return;
        }

        SW_LOG_TRACE_ID( "AstVisitor.REFLECTClassPropsMethods", "REFLECT class : %#  (props=%# methods=%# abstract=%# static=%# body=%# factory=%#)",
                         typeInfo._fullyQualifiedName, typeInfo._listProperty.size(), typeInfo._listMethod.size(),
                         typeInfo._bAbstract ? 1 : 0, typeInfo._bStatic ? 1 : 0, typeInfo._bReflectBody ? 1 : 0,
                         typeInfo._bComponentFactory ? 1 : 0 );
        outHeader._listType.push_back( std::move( typeInfo ) );
    }

    /**
     * @brief `ENUM(...)` 매크로가 붙은 열거형(Enum / Enum Class) 선언을 파싱합니다.
     *
     * 수집 단계:
     * 1. 열거형의 모든 원소(Enumerator) 이름과 정수 값
     * 2. `ENUM(...)` 어노테이션 속성(Alias, Count, Invalid 등)
     * 3. 비트플래그 여부는 `ENUM( Flags )` 선언으로만 정합니다(값 모양으로 추측하지 않습니다. 본문 주석 참고).
     */
    CXChildVisitResult AstVisitor::collectEnumeratorCursor( const CXCursor cursor, CXCursor, const CXClientData clientData )
    {
        if ( clang_getCursorKind( cursor ) == CXCursor_EnumConstantDecl )
            static_cast<vector<CXCursor>*>( clientData )->push_back( cursor );
        return CXChildVisit_Continue;
    }

    bool AstVisitor::applyEnumeratorAnnotation( const CXCursor enumCursor, ParsedEnumInfo& inoutEnum ) const
    {
        vector<CXCursor> listCursor;
        clang_visitChildren( enumCursor, collectEnumeratorCursor, &listCursor );
        if ( listCursor.size() != inoutEnum._listEnumerator.size() )
        {
            SW_LOG_ERROR_ID( "AstVisitor.EnumeratorCursorMismatch", "Enumerator cursors (%#) differ from collected enumerators (%#) on %#", listCursor.size(),
                             inoutEnum._listEnumerator.size(), inoutEnum._fullyQualifiedName );
            return false;
        }
        bool bValid = true;
        for ( size_t index = 0; index < listCursor.size(); ++index )
        {
            // 열거자 뒤의 속성은 그 열거자의 자식 AnnotateAttr 다.
            const string spelling = AnnotationLookup::findAnnotateAttr( listCursor[index], annotation::kEnumeratorPrefix );
            if ( spelling.empty() )
                continue;
            ParsedEnumeratorInfo&                    enumerator = inoutEnum._listEnumerator[index];
            StringBuilder<constant::buffer::kMax256> owner;
            owner.appendFormat( "%#::%#", inoutEnum._fullyQualifiedName, enumerator._name );
            bValid = AnnotationLookup::applyAnnotation( spelling, enumerator, owner.view() ) && bValid;
        }
        return bValid;
    }

    void AstVisitor::onEnumDeclaration( const CXCursor cursor, const string& spelling, ParsedHeader& outHeader )
    {
        ParsedEnumInfo enumInfo;
        enumInfo._name               = CursorReader::getCursorSpelling( cursor );
        enumInfo._fullyQualifiedName = CursorReader::makeFullyQualifiedName( cursor );

        BLOCK( "Collect forward-declaration facts" )
        {
            // ENUM(Flags) 의 비트 연산자 트레이트는 이 열거형을 전방 선언 한 뒤 특수화한다
            // (`CodeGenerator::makeHeaderText`). 그래서 기반 정수 타입이 필요하다. 원본 철자로
            // 받아야 `uint8` 같은 별칭이 아니라 `unsigned char` 가 나와 재선언이 어긋나지 않는다.
            enumInfo._underlyingType = CursorReader::takeString(
                clang_getTypeSpelling( clang_getCanonicalType( clang_getEnumDeclIntegerType( cursor ) ) ) );

            // 클래스 안에 든 열거형은 밖에서 전방 선언할 수 없다. 코드젠이 그 사실을 알아야
            // 조용히 깨진 헤더를 내보내지 않고 그 자리에서 알린다.
            const CXCursorKind parentKind = clang_getCursorKind( clang_getCursorSemanticParent( cursor ) );
            const bool         bNested    = parentKind == CXCursor_ClassDecl || parentKind == CXCursor_StructDecl ||
                                 parentKind == CXCursor_ClassTemplate || parentKind == CXCursor_UnionDecl;
            enumInfo._bNestedInType = bNested ? SW_TRUE : SW_FALSE;
        }

        bool bHidden = false;
        if ( resolveHiddenNested( cursor, enumInfo._fullyQualifiedName, bHidden ) == false )
        {
            markHeaderError( outHeader );
            return;
        }
        enumInfo._bHidden = bHidden ? SW_TRUE : SW_FALSE;

        // 모든 열거자 항목(이름, 정수값) 수집
        CursorReader::collectEnumerators( cursor, enumInfo._listEnumerator );
        if ( applyEnumeratorAnnotation( cursor, enumInfo ) == false )
        {
            markHeaderError( outHeader );
            return;
        }

        BLOCK( "Parse ENUM annotation (Alias / …)" )
        {
            if ( AnnotationLookup::applyAnnotation( spelling, enumInfo, enumInfo._fullyQualifiedName ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
            enumInfo._countEnumerator = CursorReader::resolveCountEnumerator( enumInfo._countEnumerator, enumInfo._listEnumerator, enumInfo._bIsBitFlag != SW_FALSE );
            if ( enumInfo._countEnumerator.empty() == false && enumInfo._invalidEnumerator.empty() )
                enumInfo._invalidEnumerator = enumInfo._countEnumerator;
            if ( DeclarationValidator::validateEnumeratorRedirects( enumInfo ) == false )
            {
                markHeaderError( outHeader );
                return;
            }
        }

        // 비트플래그인지는 선언이 정한다. 값의 모양이 아니다. 비트플래그 열거형은 `ENUM( Flags )` 로 명시한다.
        //
        // 주의: "0 이 아닌 값이 모두 2의 거듭제곱이면 BitFlag" 같은 자동 감지를 두지 말 것 — `{ Game = 0, Editor = 1, Custom = 2 }`
        // 같은 평범한 연속 열거형에도 맞아 문자열 변환이 `toStringFlags` 로 가고 인스펙터가 콤보 대신 체크박스를 그리며,
        // C++ 트레이트(`IsBitFlagEnum<>`, 명시한 `ENUM( Flags )` 로만 나간다)와 레지스트리가 다른 답을 낸다. 명시하지 않아 놓친 쪽은
        // 비트 연산자가 없어 컴파일이 그 자리에서 막히지만, 명시하지 않았는데 켜지는 쪽은 조용히 틀린다.

        SW_LOG_INFO_ID( "AstVisitor.ENUMValuesBIsBitFlagAliases", "ENUM          : %#  (%# values, _bIsBitFlag=%# aliases=%#)",
                        enumInfo._fullyQualifiedName, enumInfo._listEnumerator.size(), enumInfo._bIsBitFlag ? "true" : "false",
                        enumInfo._listAlias.size() );
        outHeader._listEnum.push_back( std::move( enumInfo ) );
    }
} // namespace sw
