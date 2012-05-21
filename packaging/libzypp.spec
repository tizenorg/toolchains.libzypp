%define run_testsuite 0
Name:           libzypp
License:        GPL v2 or later
Group:          System/Packages
AutoReqProv:    on
Summary:        Package, Patch, Pattern, and Product Management
Version:        9.8.3
Release:        1
Source:         %{name}-%{version}.tar.bz2
Source1:        %{name}-rpmlintrc
Source2:        libzypp.conf
BuildRequires:  cmake
BuildRequires:  libudev-devel
BuildRequires:  libsatsolver-devel >= 0.14.9
BuildRequires:  openssl-devel
BuildRequires:  boost-devel curl-devel 
#BuildRequires:  dejagnu doxygen  graphviz
BuildRequires:  gcc-c++ gettext-devel libxml2-devel
# required for testsuite, webrick
#BuildRequires:  ruby
BuildRequires:  expat-devel
BuildRequires:  glib2-devel popt-devel rpm-devel
BuildRequires:  pkgconfig(dbus-glib-1)
#BuildRequires:  pkgconfig(libproxy-1.0)
#Requires:       gnupg2
Requires:       satsolver-tools

Patch0: 	libzypp-6.29.2-meego.patch
Patch1: 	libzypp-log-issue-bug704.patch
Patch2:         libzypp-meego-release.patch
Patch3:		use_gpg2.patch
Patch5:         meego-check-products-dir-while-using-rpmdb2solv.patch
Patch6:         MeeGo-resume-download.patch
Patch10:        MeeGo-dont-use-multcurl-by-default.patch
Patch11:        MeeGo-Add-Rpm-Checker.patch
Patch12:        MeeGo-use-fullname-in-search_deltafile.patch
Patch13:        MeeGo-patch-readd-thumb-arch-definitions.patch
Patch14:        linker.patch
Patch15:	0001-Disable-proxy-only-if-_none_-is-set-in-repo-file.patch 
Patch16:        meego-try-again-while-downloading-fails.patch
Patch17:	docs.patch

%description
Package, Patch, Pattern, and Product Management

Authors:
--------
    Michael Andres <ma@suse.de>
    Jiri Srain <jsrain@suse.cz>
    Stefan Schubert <schubi@suse.de>
    Duncan Mac-Vicar <dmacvicar@suse.de>
    Klaus Kaempf <kkaempf@suse.de>
    Marius Tomaschewski <mt@suse.de>
    Stanislav Visnovsky <visnov@suse.cz>
    Ladislav Slezak <lslezak@suse.cz>

%package devel
License:        GPL v2 or later
Requires:       libzypp == %{version}
Requires:       libxml2-devel curl-devel openssl-devel rpm-devel glibc-devel zlib-devel
Requires:       bzip2 popt-devel glib2-devel boost-devel libstdc++-devel
Requires:       pkgconfig(dbus-1)
Requires:       cmake libsatsolver-devel >= 0.13.0
Summary:        Package, Patch, Pattern, and Product Management - developers files
Group:          System/Packages
Provides:       yast2-packagemanager-devel
Obsoletes:      yast2-packagemanager-devel

%description -n libzypp-devel
Package, Patch, Pattern, and Product Management - developers files

Authors:
--------
    Michael Andres <ma@suse.de>
    Jiri Srain <jsrain@suse.cz>
    Stefan Schubert <schubi@suse.de>
    Duncan Mac-Vicar <dmacvicar@suse.de>
    Klaus Kaempf <kkaempf@suse.de>
    Marius Tomaschewski <mt@suse.de>
    Stanislav Visnovsky <visnov@suse.cz>
    Ladislav Slezak <lslezak@suse.cz>

%prep
%setup -q
%patch0 -p1 -b .meego
%patch1 -p1 -b .log-issue
%patch2 -p1 -b .meego-release
%patch3 -p1 
%patch5 -p1
%patch6 -p1
%patch10 -p1
%patch11 -p1
%patch12 -p1
%patch13 -p1
%patch14 -p1
%patch15 -p1
%patch16 -p1
%patch17 -p1

%build
mkdir build
cd build
export CFLAGS="$RPM_OPT_FLAGS"
export CXXFLAGS="$CFLAGS"

cmake -DCMAKE_INSTALL_PREFIX=/usr \
      -DDOC_INSTALL_DIR=%{_docdir} \
      -DLIB=%{_lib} \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SKIP_RPATH=1 \
      ..
make %{?jobs:-j %jobs} VERBOSE=1
make -C po %{?jobs:-j %jobs} translations
%if 0%{?run_testsuite}
  make -C tests %{?jobs:-j %jobs}
  pushd tests
  LD_LIBRARY_PATH=$PWD/../zypp:$LD_LIBRARY_PATH ctest .
  popd
%endif

%install
rm -rf "$RPM_BUILD_ROOT"
cd build
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc/zypp/repos.d
mkdir -p $RPM_BUILD_ROOT/etc/zypp/services.d
mkdir -p $RPM_BUILD_ROOT/%{_usr}/lib/zypp
mkdir -p $RPM_BUILD_ROOT/%{_var}/lib/zypp
mkdir -p $RPM_BUILD_ROOT/%{_var}/log/zypp
mkdir -p $RPM_BUILD_ROOT/%{_var}/cache/zypp
make -C po install DESTDIR=$RPM_BUILD_ROOT
# Create filelist with translations
cd ..

install -d %{buildroot}/etc/prelink.conf.d/
install -m 644 %{SOURCE2} %{buildroot}/etc/prelink.conf.d/


%{find_lang} zypp

%post
/sbin/ldconfig
if [ -f /var/cache/zypp/zypp.db ]; then rm /var/cache/zypp/zypp.db; fi
# convert old lock file to new
# TODO make this a separate file?
# TODO run the sript only when updating form pre-11.0 libzypp versions
LOCKSFILE=/etc/zypp/locks
OLDLOCKSFILE=/etc/zypp/locks.old
is_old(){
  # if no such file, exit with false (1 in bash)
  test -f ${LOCKSFILE} || return 1
  TEMP_FILE=`mktemp`
  cat ${LOCKSFILE} | sed '/^\#.*/ d;/.*:.*/d;/^[^[a-zA-Z\*?.0-9]*$/d' > ${TEMP_FILE}
  if [ -s ${TEMP_FILE} ]
  then
    RES=0
  else
    RES=1
  fi
  rm -f ${TEMP_FILE}
  return ${RES}
}
append_new_lock(){
  case "$#" in
    1 )
  echo "
solvable_name: $1
match_type: glob
" >> ${LOCKSFILE}
;;
    2 ) #TODO version
  echo "
solvable_name: $1
match_type: glob
version: $2
" >> ${LOCKSFILE}
;;
    3 ) #TODO version
  echo "
solvable_name: $1
match_type: glob
version: $2 $3
" >> ${LOCKSFILE}
  ;;
esac
}
die() {
  echo $1
  exit 1
}
if is_old ${LOCKSFILE}
  then
  mv -f ${LOCKSFILE} ${OLDLOCKSFILE} || die "cannot backup old locks"
  cat ${OLDLOCKSFILE}| sed "/^\#.*/d"| while read line
  do
    append_new_lock $line
  done
fi

%postun -p /sbin/ldconfig

%clean
rm -rf "$RPM_BUILD_ROOT"

%files -f zypp.lang
%defattr(-,root,root,-)
%dir /etc/zypp
%dir /etc/zypp/repos.d
%dir /etc/zypp/services.d
%config(noreplace) /etc/zypp/zypp.conf
%config(noreplace) /etc/zypp/systemCheck
%config(noreplace) %{_sysconfdir}/logrotate.d/zypp-history.lr
%config(noreplace) /etc/prelink.conf.d/*
%dir %{_libdir}/zypp
%{_libdir}/zypp
%dir %{_var}/lib/zypp
%dir %{_var}/log/zypp
%dir %{_var}/cache/zypp
/usr/share/zypp
/usr/bin/*
%{_libdir}/libzypp*so.*

%files devel
%defattr(-,root,root,-)
%{_libdir}/libzypp.so
%dir /usr/include/zypp
/usr/include/zypp/*
/usr/share/cmake/Modules/*
%{_libdir}/pkgconfig/libzypp.pc