/*---------------------------------------------------------------------\
|                          ____ _   __ __ ___                          |
|                         |__  / \ / / . \ . \                         |
|                           / / \ V /|  _/  _/                         |
|                          / /__ | | | | | |                           |
|                         /_____||_| |_| |_|                           |
|                                                                      |
\---------------------------------------------------------------------*/
/** \file zypp/media/MediaCD.cc
 *
*/
extern "C"
{
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#if HAVE_UDEV
#include <libudev.h>
#endif
}

#ifndef HAVE_UDEV
#if HAVE_HAL
#include "zypp/target/hal/HalContext.h"
#endif
#endif

#include <cstring> // strerror
#include <cstdlib> // getenv
#include <iostream>

#include "zypp/base/Logger.h"
#include "zypp/ExternalProgram.h"
#include "zypp/media/Mount.h"
#include "zypp/media/MediaCD.h"
#include "zypp/media/MediaManager.h"
#include "zypp/Url.h"
#include "zypp/AutoDispose.h"



/*
** if to throw exception on eject errors or ignore them
*/
#define  REPORT_EJECT_ERRORS     1

/*
** If defined to the full path of the eject utility,
** it will be used additionally to the eject-ioctl.
*/
#define EJECT_TOOL_PATH "/bin/eject"


using namespace std;

namespace zypp {
  namespace media {

///////////////////////////////////////////////////////////////////
//
//  CLASS NAME : MediaCD
//
///////////////////////////////////////////////////////////////////


  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::MediaCD
  //  METHOD TYPE : Constructor
  //
  //  DESCRIPTION :
  //
  MediaCD::MediaCD( const Url &      url_r,
                    const Pathname & attach_point_hint_r )
    : MediaHandler( url_r, attach_point_hint_r,
                    url_r.getPathName(), // urlpath below attachpoint
                    false ),
      _lastdev(-1), _lastdev_tried(-1)
  {
    MIL << "MediaCD::MediaCD(" << url_r << ", " << attach_point_hint_r << ")"
        << endl;

    if( url_r.getScheme() != "dvd" && url_r.getScheme() != "cd")
    {
      ERR << "Unsupported schema in the Url: " << url_r.asString() << endl;
      ZYPP_THROW(MediaUnsupportedUrlSchemeException(_url));
    }

    string devices = _url.getQueryParam("devices");
    if (!devices.empty())
    {
      string::size_type pos;
      DBG << "parse " << devices << endl;
      while(!devices.empty())
      {
        pos = devices.find(',');
        string device = devices.substr(0,pos);
        if (!device.empty())
        {
          MediaSource media("cdrom", device, 0, 0);
          _devices.push_back( media);
          DBG << "use device (delayed verify)" << device << endl;
        }
        if (pos!=string::npos)
          devices=devices.substr(pos+1);
        else
          devices.erase();
      }
    }
    else
    {
      DBG << "going to use on-demand device list" << endl;
      return;
    }

    if( _devices.empty())
    {
      ERR << "Unable to find any cdrom drive for " << _url.asString() << endl;
      ZYPP_THROW(MediaBadUrlEmptyDestinationException(_url));
    }
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::openTray
  //  METHOD TYPE : bool
  //
  bool MediaCD::openTray( const std::string & device_r )
  {
    int fd = ::open( device_r.c_str(), O_RDONLY|O_NONBLOCK );
    int res = -1;

    if ( fd != -1)
    {
      res = ::ioctl( fd, CDROMEJECT );
      ::close( fd );
    }

    if ( res )
    {
      if( fd == -1)
      {
        WAR << "Unable to open '" << device_r
            << "' (" << ::strerror( errno ) << ")" << endl;
      }
      else
      {
        WAR << "Eject " << device_r
            << " failed (" << ::strerror( errno ) << ")" << endl;
      }

#if defined(EJECT_TOOL_PATH)
      DBG << "Try to eject " << device_r << " using "
        << EJECT_TOOL_PATH << " utility" << std::endl;

      const char *cmd[3];
      cmd[0] = EJECT_TOOL_PATH;
      cmd[1] = device_r.c_str();
      cmd[2] = NULL;
      ExternalProgram eject(cmd, ExternalProgram::Stderr_To_Stdout);

      for(std::string out( eject.receiveLine());
          out.length(); out = eject.receiveLine())
      {
        DBG << " " << out;
      }

      if(eject.close() != 0)
      {
        WAR << "Eject of " << device_r << " failed." << std::endl;
        return false;
      }
#else
      return false;
#endif
    }
    MIL << "Eject of " << device_r << " successful." << endl;
    return true;
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //	METHOD NAME : MediaCD::closeTray
  //	METHOD TYPE : bool
  //
  bool MediaCD::closeTray( const std::string & device_r )
  {
    int fd = ::open( device_r.c_str(), O_RDONLY|O_NONBLOCK );
    if ( fd == -1 ) {
      WAR << "Unable to open '" << device_r << "' (" << ::strerror( errno ) << ")" << endl;
      return false;
    }
    int res = ::ioctl( fd, CDROMCLOSETRAY );
    ::close( fd );
    if ( res ) {
      WAR << "Close tray " << device_r << " failed (" << ::strerror( errno ) << ")" << endl;
      return false;
    }
    DBG << "Close tray " << device_r << endl;
    return true;
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::detectDevices
  //  METHOD TYPE : MediaCD::DeviceList
  //
  MediaCD::DeviceList MediaCD::detectDevices( bool supportingDVD ) const
  {
    DeviceList detected;
#ifdef HAVE_UDEV
    // http://www.kernel.org/pub/linux/utils/kernel/hotplug/libudev/index.html
    zypp::AutoDispose<struct udev *> udev( ::udev_new(), ::udev_unref );
    if ( ! udev )
    {
      ERR << "Can't create udev context." << endl;
      return DeviceList();
    }

    zypp::AutoDispose<struct udev_enumerate *> enumerate( ::udev_enumerate_new(udev), ::udev_enumerate_unref );
    if ( ! enumerate )
    {
      ERR << "Can't create udev list entry." << endl;
      return DeviceList();
    }

    ::udev_enumerate_add_match_subsystem( enumerate, "block" );
    ::udev_enumerate_add_match_property( enumerate, "ID_CDROM", "1" );
    ::udev_enumerate_scan_devices( enumerate );

    struct udev_list_entry * entry = 0;
    udev_list_entry_foreach( entry, ::udev_enumerate_get_list_entry( enumerate ) )
    {
      zypp::AutoDispose<struct udev_device *> device( ::udev_device_new_from_syspath( ::udev_enumerate_get_udev( enumerate ),
										      ::udev_list_entry_get_name( entry ) ),
						      ::udev_device_unref );
      if ( ! device )
      {
	ERR << "Can't create udev device." << endl;
	continue;
      }

      if ( supportingDVD && ! ::udev_device_get_property_value( device, "ID_CDROM_DVD" ) )
      {
	continue;	// looking for dvd only
      }

      const char * devnodePtr( ::udev_device_get_devnode( device ) );
      if ( ! devnodePtr )
      {
	ERR << "Got NULL devicenode." << endl;
	continue;
      }

      // In case we need it someday:
      //const char * mountpath = ::udev_device_get_property_value( device, "FSTAB_DIR" );

      PathInfo devnode( devnodePtr );
      if ( devnode.isBlk() )
      {
	MediaSource media( "cdrom", devnode.path().asString(), devnode.major(), devnode.minor() );
	DBG << "Found (udev): " << media << std::endl;
	detected.push_back( media );
      }
    }
    if ( detected.empty() )
      WAR << "Did not find any CD/DVD device." << endl;
#elif HAVE_HAL
    using namespace zypp::target::hal;
    try
    {
      HalContext hal(true);

      std::vector<std::string> drv_udis;
      drv_udis = hal.findDevicesByCapability("storage.cdrom");

      DBG << "Found " << drv_udis.size() << " cdrom drive udis" << std::endl;
      for(size_t d = 0; d < drv_udis.size(); d++)
      {
        HalDrive drv( hal.getDriveFromUDI( drv_udis[d]));

        if( drv)
        {
          bool supportsDVD=false;
          if( supportingDVD)
          {
            std::vector<std::string> caps;
            try {
              caps = drv.getCdromCapabilityNames();
            }
            catch(const HalException &e)
            {
              ZYPP_CAUGHT(e);
            }

            std::vector<std::string>::const_iterator ci;
            for( ci=caps.begin(); ci != caps.end(); ++ci)
            {
              if( *ci == "dvd")
                supportsDVD = true;
            }
          }

          MediaSource media("cdrom", drv.getDeviceFile(),
                            drv.getDeviceMajor(),
                            drv.getDeviceMinor());
          DBG << "Found " << drv_udis[d] << ": "
              << media.asString() << std::endl;
          if( supportingDVD && supportsDVD)
          {
            detected.push_front(media);
          }
          else
          {
            detected.push_back(media);
          }
        }
      }
    }
    catch(const zypp::target::hal::HalException &e)
    {
      ZYPP_CAUGHT(e);
    }
#else // manual way
#warning Poor CDROM devices detection without udev/HAL
    WAR << "Cdrom drive detection without HAL! " << std::endl;
    PathInfo dvdinfo( "/dev/dvd" );
    PathInfo cdrinfo( "/dev/cdrom" );
    if ( dvdinfo.isBlk() )
    {
      MediaSource media( "cdrom", dvdinfo.path().asString(), dvdinfo.major(), dvdinfo.minor() );
      DBG << "Found (NO_HAL): " << media << std::endl;
      detected.push_back( media );
    }
    if ( cdrinfo.isBlk()
         && ! ( cdrinfo.major() == dvdinfo.major() && cdrinfo.minor() == dvdinfo.minor() ) )
    {
      MediaSource media( "cdrom", cdrinfo.path().asString(), cdrinfo.major(), cdrinfo.minor() );
      DBG << "Found (NO_HAL): " << media << std::endl;
      detected.push_back( media );
    }
#endif
    return detected;
  }


  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::attachTo
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that not already attached, and attachPoint is a directory.
  //
  void MediaCD::attachTo(bool next)
  {
    DBG << "next " << next << " last " << _lastdev << " last tried " << _lastdev_tried << endl;
    if (next && _lastdev == -1)
      ZYPP_THROW(MediaNotSupportedException(url()));

    DeviceList detected(
      detectDevices(_url.getScheme() == "dvd" ? true : false));

    if(_devices.empty())
    {
      DBG << "creating on-demand device list" << endl;
      //default is /dev/cdrom; for dvd: /dev/dvd if it exists
      string device( "/dev/cdrom" );
      if ( _url.getScheme() == "dvd" && PathInfo( "/dev/dvd" ).isBlk() )
      {
        device = "/dev/dvd";
      }

      PathInfo dinfo(device);
      if( dinfo.isBlk())
      {
        MediaSource media("cdrom", device, dinfo.major(), dinfo.minor());

        DeviceList::const_iterator d( detected.begin());
        for( ; d != detected.end(); ++d)
        {
          // /dev/cdrom or /dev/dvd to the front
          if( media.equals( *d))
            _devices.push_front( *d);
          else
            _devices.push_back( *d);
        }
      }
      else
      {
        // no /dev/cdrom or /dev/dvd link
        _devices = detected;
      }
    }

    Mount mount;
    string mountpoint = attachPoint().asString();
    bool mountsucceeded = false;
    int count = 0;
    MediaMountException merr;

    string options = _url.getQueryParam("mountoptions");
    if (options.empty())
    {
      options="ro";
    }

    //TODO: make configurable
    list<string> filesystems;

    // if DVD, try UDF filesystem before iso9660
    if ( _url.getScheme() == "dvd" )
      filesystems.push_back("udf");

    filesystems.push_back("iso9660");

    // try all devices in sequence
    for (DeviceList::iterator it = _devices.begin()
    ; !mountsucceeded && it != _devices.end()
    ; ++it, count++ )
    {
      DBG << "count " << count << endl;
      if (next && count <=_lastdev_tried )
      {
        DBG << "skipping device " << it->name << endl;
        continue;
      }

      _lastdev_tried = count;

      MediaSource temp( *it);
      bool        valid=false;
      PathInfo    dinfo(temp.name);
      if( dinfo.isBlk())
      {
        temp.maj_nr = dinfo.major();
        temp.min_nr = dinfo.minor();

        DeviceList::const_iterator d( detected.begin());
        for( ; d != detected.end(); ++d)
        {
          if( temp.equals( *d))
          {
            valid = true;
            break;
          }
        }
      }
      if( !valid)
      {
        DBG << "skipping invalid device: " << it->name << endl;
        continue;
      }

      MediaSourceRef media( new MediaSource(temp));
      AttachedMedia ret( findAttachedMedia( media));

      if( ret.mediaSource && ret.attachPoint &&
         !ret.attachPoint->empty())
      {
        DBG << "Using a shared media "
            << ret.mediaSource->name
            << " attached on "
            << ret.attachPoint->path
            << endl;
        removeAttachPoint();
        setAttachPoint(ret.attachPoint);
        setMediaSource(ret.mediaSource);
        _lastdev = count;
        mountsucceeded = true;
        break;
      }

      {
        MediaManager  manager;
        MountEntries  entries( manager.getMountEntries());
        MountEntries::const_iterator e;
        for( e = entries.begin(); e != entries.end(); ++e)
        {
          bool        is_device = false;
          std::string dev_path(Pathname(e->src).asString());
          PathInfo    dev_info;

          if( dev_path.compare(0, sizeof("/dev/")-1, "/dev/") == 0 &&
              dev_info(e->src) && dev_info.isBlk())
          {
            is_device = true;
          }

          if( is_device && media->maj_nr == dev_info.major() &&
                           media->min_nr == dev_info.minor())
          {
            AttachPointRef ap( new AttachPoint(e->dir, false));
            AttachedMedia  am( media, ap);
            {
              DBG << "Using a system mounted media "
                  << media->name
                  << " attached on "
                  << ap->path
                  << endl;

              media->iown = false; // mark attachment as foreign

              setMediaSource(media);
              setAttachPoint(ap);
              _lastdev = count;
              mountsucceeded = true;
              break;
            }
          }
        }
        if( mountsucceeded)
          break;
      }

      // close tray
      closeTray( it->name );

      // try all filesystems in sequence
      for(list<string>::iterator fsit = filesystems.begin()
          ; !mountsucceeded && fsit != filesystems.end()
          ; ++fsit)
      {
        try
        {
          if( !isUseableAttachPoint(Pathname(mountpoint)))
          {
            mountpoint = createAttachPoint().asString();
            setAttachPoint( mountpoint, true);
            if( mountpoint.empty())
            {
              ZYPP_THROW( MediaBadAttachPointException(url()));
            }
          }

          mount.mount(it->name, mountpoint, *fsit, options);

          setMediaSource(media);

          // wait for /etc/mtab update ...
          // (shouldn't be needed)
          int limit = 5;
          while( !(mountsucceeded=isAttached()) && --limit)
          {
            sleep(1);
          }

          if( mountsucceeded)
          {
            _lastdev = count;
          }
          else
          {
            setMediaSource(MediaSourceRef());
            try
            {
              mount.umount(attachPoint().asString());
            }
            catch (const MediaException & excpt_r)
            {
              ZYPP_CAUGHT(excpt_r);
            }
            ZYPP_THROW(MediaMountException(
              "Unable to verify that the media was mounted",
              it->name, mountpoint
            ));
          }
        }
        catch (const MediaMountException &e)
        {
          merr = e;
          removeAttachPoint();
          ZYPP_CAUGHT(e);
        }
        catch (const MediaException & excpt_r)
        {
          removeAttachPoint();
          ZYPP_CAUGHT(excpt_r);
        }
      } // for filesystems
    } // for _devices

    if (!mountsucceeded)
    {
      _lastdev = -1;

      if( !merr.mountOutput().empty())
      {
        ZYPP_THROW(MediaMountException(merr.mountError(),
                                       _url.asString(),
                                       mountpoint,
                                       merr.mountOutput()));
      }
      else
      {
        ZYPP_THROW(MediaMountException("Mounting media failed",
                                       _url.asString(), mountpoint));
      }
    }
    DBG << _lastdev << " " << count << endl;
  }


  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::releaseFrom
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that media is attached.
  //
  void MediaCD::releaseFrom( const std::string & ejectDev )
  {
    Mount mount;
    try
    {
      AttachedMedia am( attachedMedia());
      if(am.mediaSource && am.mediaSource->iown)
        mount.umount(am.attachPoint->path.asString());
    }
    catch (const Exception & excpt_r)
    {
      ZYPP_CAUGHT(excpt_r);
      if (!ejectDev.empty())
      {
        forceRelaseAllMedia(false);
        if(openTray( ejectDev ))
          return;
      }
      ZYPP_RETHROW(excpt_r);
    }

    // eject device
    if (!ejectDev.empty())
    {
      forceRelaseAllMedia(false);
      if( !openTray( ejectDev ))
      {
#if REPORT_EJECT_ERRORS
        ZYPP_THROW(MediaNotEjectedException(ejectDev));
#endif
      }
    }
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::forceEject
  //  METHOD TYPE : void
  //
  // Asserted that media is not attached.
  //
  void MediaCD::forceEject(const std::string & ejectDev)
  {
    bool ejected=false;
    if ( !isAttached()) {	// no device mounted in this instance

      DeviceList detected(
          detectDevices(_url.getScheme() == "dvd" ? true : false));

      if(_devices.empty())
      {
        DBG << "creating on-demand device list" << endl;
        //default is /dev/cdrom; for dvd: /dev/dvd if it exists
        string device( "/dev/cdrom" );
        if ( _url.getScheme() == "dvd" && PathInfo( "/dev/dvd" ).isBlk() ) {
          device = "/dev/dvd";
        }

        PathInfo dinfo(device);
        if( dinfo.isBlk())
        {
          MediaSource media("cdrom", device, dinfo.major(), dinfo.minor());

          DeviceList::const_iterator d( detected.begin());
          for( ; d != detected.end(); ++d)
          {
            // /dev/cdrom or /dev/dvd to the front
            if( media.equals( *d))
              _devices.push_front( *d);
            else
              _devices.push_back( *d);
          }
        }
        else
        {
          // no /dev/cdrom or /dev/dvd link
          _devices = detected;
        }
      }

      DeviceList::iterator it;
      for( it = _devices.begin(); it != _devices.end(); ++it ) {
        MediaSourceRef media( new MediaSource( *it));
        if (media->name != ejectDev)
          continue;

        bool        valid=false;
        PathInfo    dinfo(media->name);
        if( dinfo.isBlk())
        {
          media->maj_nr = dinfo.major();
          media->min_nr = dinfo.minor();

          DeviceList::const_iterator d( detected.begin());
          for( ; d != detected.end(); ++d)
          {
            if( media->equals( *d))
            {
              valid = true;
              break;
            }
          }
        }
        if( !valid)
        {
          DBG << "skipping invalid device: " << it->name << endl;
          continue;
        }

        // FIXME: we have also to check if it is mounted in the system
        AttachedMedia ret( findAttachedMedia( media));
        if( !ret.mediaSource)
        {
          forceRelaseAllMedia(media, false);
          if ( openTray( it->name ) )
          {
            ejected = true;
            break; // on 1st success
          }
        }
      }
    }
    if( !ejected)
    {
#if REPORT_EJECT_ERRORS
      ZYPP_THROW(MediaNotEjectedException());
#endif
    }
  }

  ///////////////////////////////////////////////////////////////////
  //
  //  METHOD NAME : MediaCD::isAttached
  //  METHOD TYPE : bool
  //
  //  DESCRIPTION : Override check if media is attached.
  //
  bool
  MediaCD::isAttached() const
  {
    return checkAttached(false);
  }

  ///////////////////////////////////////////////////////////////////
  //
  //  METHOD NAME : MediaCD::getFile
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that media is attached.
  //
  void MediaCD::getFile( const Pathname & filename ) const
  {
    MediaHandler::getFile( filename );
  }

  ///////////////////////////////////////////////////////////////////
  //
  //  METHOD NAME : MediaCD::getDir
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that media is attached.
  //
  void MediaCD::getDir( const Pathname & dirname, bool recurse_r ) const
  {
    MediaHandler::getDir( dirname, recurse_r );
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::getDirInfo
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that media is attached and retlist is empty.
  //
  void MediaCD::getDirInfo( std::list<std::string> & retlist,
                            const Pathname & dirname, bool dots ) const
  {
    MediaHandler::getDirInfo( retlist, dirname, dots );
  }

  ///////////////////////////////////////////////////////////////////
  //
  //
  //  METHOD NAME : MediaCD::getDirInfo
  //  METHOD TYPE : PMError
  //
  //  DESCRIPTION : Asserted that media is attached and retlist is empty.
  //
  void MediaCD::getDirInfo( filesystem::DirContent & retlist,
                            const Pathname & dirname, bool dots ) const
  {
    MediaHandler::getDirInfo( retlist, dirname, dots );
  }

  bool MediaCD::getDoesFileExist( const Pathname & filename ) const
  {
    return MediaHandler::getDoesFileExist( filename );
  }

  bool MediaCD::hasMoreDevices()
  {
    if (_devices.size() == 0)
      return false;
    else if (_lastdev_tried < 0)
      return true;

    return (unsigned) _lastdev_tried < _devices.size() - 1;
  }

  void
  MediaCD::getDetectedDevices(std::vector<std::string> & devices,
                              unsigned int & index) const
  {
    index = 0;
    if (!devices.empty())
      devices.clear();

    for (DeviceList::const_iterator it = _devices.begin();
         it != _devices.end(); ++it)
      devices.push_back(it->name);

    if (_lastdev >= 0)
      index = _lastdev;

    // try to detect again if _devices are empty (maybe this method will be
    // called before _devices get actually filled)
    if (devices.empty())
    {
      DBG << "no device list so far, trying to detect" << endl;

      DeviceList detected(
        detectDevices(_url.getScheme() == "dvd" ? true : false));

      for (DeviceList::const_iterator it = detected.begin();
           it != detected.end(); ++it)
        devices.push_back(it->name);

      // don't know which one is in use in this case
      index = 0;
    }

    MIL << "got " << devices.size() << " detected devices, current: "
        << (index < devices.size() ? devices[index] : "<none>")
        << "(" << index << ")" << endl;
  }


  } // namespace media
} // namespace zypp
// vim: set ts=8 sts=2 sw=2 ai noet: