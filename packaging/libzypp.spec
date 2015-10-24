Name:           libzypp
License:        GPL-2.0+
Group:          System/Package Management
Summary:        Package, Patch, Pattern, and Product Management
Version:        14.27.0
Release:        1
Source:         %{name}-%{version}.tar.bz2
Source1:        %{name}-rpmlintrc
Source1001: 	libzypp.manifest

# Features we provide (update doc/autoinclude/FeatureTest.doc):
Provides:       libzypp(plugin) = 0
Provides:       libzypp(plugin:commit) = 0
Provides:       libzypp(plugin:services) = 0
Provides:       libzypp(plugin:system) = 0
Provides:       libzypp(plugin:urlresolver) = 0

BuildRequires:  cmake
BuildRequires:  openssl-devel
BuildRequires:  libudev-devel
BuildRequires:  boost-devel
BuildRequires:  doxygen
BuildRequires:  gcc-c++ >= 4.6
BuildRequires:  gettext-devel
BuildRequires:  libxml2-devel
#BuildRequires:  pkgconfig(libproxy-1.0)

BuildRequires:  pkg-config

BuildRequires:  libsolv-devel
Requires:       libsolv-tools

# should be recommends
Requires:       lsof

BuildRequires:  expat-devel

Requires:       rpm

BuildRequires:  rpm-devel

BuildRequires:  glib2-devel
BuildRequires:  popt-devel
BuildRequires:  rpm-devel

%define min_curl_version 7.19.4
BuildRequires:  libcurl-devel >= %{min_curl_version}
Requires:       libcurl   >= %{min_curl_version}

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
Summary:        Package, Patch, Pattern, and Product Management - developers files
License:        GPL-2.0+
Requires:       libzypp = %{version}
Requires:       libxml2-devel
Requires:       openssl-devel
Requires:       rpm-devel
Requires:       glibc-devel
Requires:       zlib-devel
Requires:       bzip2
Requires:       popt-devel
Requires:       boost-devel
Requires:       libstdc++-devel
Requires:       libudev-devel
Requires:       cmake
Requires:       libcurl-devel >= %{min_curl_version}
Requires:       libsolv-devel
Group:          Development/Libraries

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
cp %{SOURCE1001} .

%build
mkdir build
cd build
export CFLAGS="$RPM_OPT_FLAGS"
export CXXFLAGS="$RPM_OPT_FLAGS"
unset TRANSLATION_SET
unset EXTRA_CMAKE_OPTIONS
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DDOC_INSTALL_DIR=%{_docdir} \
      -DLIB=%{_lib} \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SKIP_RPATH=1 \
      -DUSE_TRANSLATION_SET=${TRANSLATION_SET:-zypp} \
      ${EXTRA_CMAKE_OPTIONS} \
      ..
make %{?_smp_mflags} VERBOSE=1
make -C doc/autodoc %{?_smp_mflags}
make -C po %{?_smp_mflags} translations

%if 0%{?run_testsuite}
  make -C tests %{?_smp_mflags}
  pushd tests
  LD_LIBRARY_PATH=$PWD/../zypp:$LD_LIBRARY_PATH ctest .
  popd
%endif

#make check

%install
rm -rf "$RPM_BUILD_ROOT"
cd build
make install DESTDIR=$RPM_BUILD_ROOT
make -C doc/autodoc install DESTDIR=$RPM_BUILD_ROOT
%if 0%{?fedora_version} || 0%{?rhel_version} >= 600 || 0%{?centos_version} >= 600
ln -s %{_sysconfdir}/yum.repos.d $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
%else
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
%endif
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/services.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/vendors.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/multiversion.d
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp/plugins
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp/plugins/commit
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp/plugins/services
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp/plugins/system
mkdir -p $RPM_BUILD_ROOT%{_prefix}/lib/zypp/plugins/urlresolver
mkdir -p $RPM_BUILD_ROOT%{_var}/lib/zypp
mkdir -p $RPM_BUILD_ROOT%{_var}/log/zypp
mkdir -p $RPM_BUILD_ROOT%{_var}/cache/zypp

make -C po install DESTDIR=$RPM_BUILD_ROOT
# Create filelist with translations
cd ..

find %{buildroot}/usr/share/locale/ -name "*.mo" -type f -exec rm {} \;

%post
/sbin/ldconfig
if [ -f /var/cache/zypp/zypp.db ]; then rm /var/cache/zypp/zypp.db; fi

# convert old lock file to new
# TODO make this a separate file?
# TODO run the sript only when updating form pre-11.0 libzypp versions
LOCKSFILE=%{_sysconfdir}/zypp/locks
OLDLOCKSFILE=%{_sysconfdir}/zypp/locks.old

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


%files 
%manifest %{name}.manifest
%defattr(-,root,root)
%doc           COPYING
%dir               %{_sysconfdir}/zypp
%dir               %{_sysconfdir}/zypp/repos.d
%dir               %{_sysconfdir}/zypp/services.d
%dir               %{_sysconfdir}/zypp/vendors.d
%dir               %{_sysconfdir}/zypp/multiversion.d
%config(noreplace) %{_sysconfdir}/zypp/zypp.conf
%config(noreplace) %{_sysconfdir}/zypp/systemCheck
%config(noreplace) %{_sysconfdir}/logrotate.d/zypp-history.lr
%dir               %{_var}/lib/zypp
%dir               %{_var}/log/zypp
%dir               %{_var}/cache/zypp
%{_prefix}/lib/zypp
%{_datadir}/zypp
%{_bindir}/*
%{_libdir}/libzypp*so.*
%doc %{_mandir}/man5/locks.5.*

%files devel
%manifest %{name}.manifest
%defattr(-,root,root)
%{_libdir}/libzypp.so
%{_docdir}/%{name}
%{_includedir}/zypp
%{_datadir}/cmake/Modules/*
%{_libdir}/pkgconfig/libzypp.pc

%changelog
