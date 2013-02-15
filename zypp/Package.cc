/*---------------------------------------------------------------------\
|                          ____ _   __ __ ___                          |
|                         |__  / \ / / . \ . \                         |
|                           / / \ V /|  _/  _/                         |
|                          / /__ | | | | | |                           |
|                         /_____||_| |_| |_|                           |
|                                                                      |
\---------------------------------------------------------------------*/
/** \file	zypp/Package.cc
 *
*/
#include "zypp/base/Logger.h"
#include "zypp/base/String.h"
#include "zypp/Package.h"
#include "zypp/sat/LookupAttr.h"
#include "zypp/ZYppFactory.h"
#include "zypp/target/rpm/RpmDb.h"
#include "zypp/target/rpm/RpmHeader.h"

using namespace std;

///////////////////////////////////////////////////////////////////
namespace zypp
{ /////////////////////////////////////////////////////////////////

  IMPL_PTR_TYPE(Package);

  ///////////////////////////////////////////////////////////////////
  //
  //	METHOD NAME : Package::Package
  //	METHOD TYPE : Ctor
  //
  Package::Package( const sat::Solvable & solvable_r )
  : ResObject( solvable_r )
  {}

  ///////////////////////////////////////////////////////////////////
  //
  //	METHOD NAME : Package::~Package
  //	METHOD TYPE : Dtor
  //
  Package::~Package()
  {}

  VendorSupportOption Package::vendorSupport() const
  {
    static const IdString support_unsupported( "support_unsupported" );
    static const IdString support_acc( "support_acc" );
    static const IdString support_l1( "support_l1" );
    static const IdString support_l2( "support_l2" );
    static const IdString support_l3( "support_l3" );

    Keywords kw( keywords() );
    for_( it, kw.begin(), kw.end() )
    {
      if ( *it == support_unsupported )
        return VendorSupportUnsupported;
      if ( *it == support_acc )
        return VendorSupportACC;
      if ( *it == support_l1 )
        return VendorSupportLevel1;
      if ( *it == support_l2 )
        return VendorSupportLevel2;
      if ( *it == support_l3 )
        return VendorSupportLevel3;
    }
    return VendorSupportUnknown;
  }

  bool Package::maybeUnsupported() const
  {
      if ( ( vendorSupport() == VendorSupportUnknown ) ||
           ( vendorSupport() == VendorSupportACC ) ||
           ( vendorSupport() == VendorSupportUnsupported ) )
          return true;
      return false;
  }

  Changelog Package::changelog() const
  {
      Target_Ptr target( getZYpp()->getTarget() );
      if ( ! target )
      {
        ERR << "Target not initialized. Changelog is not available." << std::endl;
        return Changelog();
      }

      if ( repository().isSystemRepo() )
      {
          target::rpm::RpmHeader::constPtr header;
          target->rpmDb().getData(name(), header);
          return header ? header->tag_changelog() : Changelog(); // might be deleted behind our back (bnc #530595)
      }
      WAR << "changelog is not available for uninstalled packages" << std::endl;
      return Changelog();
  }

  std::string Package::buildhost() const
  { return lookupStrAttribute( sat::SolvAttr::buildhost ); }

  std::string Package::distribution() const
  { return lookupStrAttribute( sat::SolvAttr::distribution ); }

  std::string Package::license() const
  { return lookupStrAttribute( sat::SolvAttr::license ); }

  std::string Package::packager() const
  { return lookupStrAttribute( sat::SolvAttr::packager ); }

  std::string Package::group() const
  { return lookupStrAttribute( sat::SolvAttr::group ); }

  Package::Keywords Package::keywords() const
  { return Keywords( sat::SolvAttr::keywords, satSolvable() ); }

  std::string Package::url() const
  { return lookupStrAttribute( sat::SolvAttr::url ); }

  ByteCount Package::sourcesize() const
  { return lookupNumAttribute( sat::SolvAttr::sourcesize ); }

  std::list<std::string> Package::authors() const
  {
    std::list<std::string> ret;
    str::split( lookupStrAttribute( sat::SolvAttr::authors ), std::back_inserter(ret), "\n" );
    return ret;
  }

  Package::FileList Package::filelist() const
  { return FileList( sat::SolvAttr::filelist, satSolvable() ); }

  CheckSum Package::checksum() const
  { return lookupCheckSumAttribute( sat::SolvAttr::checksum ); }

  OnMediaLocation Package::location() const
  { return lookupLocation(); }

  std::string Package::sourcePkgName() const
  {
    // no id means same as package
    sat::detail::IdType id( lookupIdAttribute( sat::SolvAttr::sourcename ) );
    return id ? IdString( id ).asString() : name();
  }

  Edition Package::sourcePkgEdition() const
  {
   // no id means same as package
    sat::detail::IdType id( lookupIdAttribute( sat::SolvAttr::sourceevr ) );
    return id ? Edition( id ) : edition();
  }

  std::string Package::sourcePkgType() const
  { return lookupStrAttribute( sat::SolvAttr::sourcearch ); }

  std::string Package::sourcePkgLongName() const
  { return str::form( "%s-%s.%s", sourcePkgName().c_str(), sourcePkgEdition().c_str(), sourcePkgType().c_str() ); }


  /////////////////////////////////////////////////////////////////
} // namespace zypp
///////////////////////////////////////////////////////////////////
