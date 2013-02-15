#
# spec file for package libzypp
#
# Copyright (c) 2007 SUSE LINUX Products GmbH, Nuernberg, Germany.
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#
# Please submit bugfixes or comments via http://bugs.opensuse.org/
#

# norootforbuild

Name:           @PACKAGE@
License:        GPLv2
Group:          System/Packages
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Summary:        Package, Patch, Pattern, and Product Management
Version:        @VERSION@
Release:        0
Source:         %{name}-%{version}.tar.bz2
Source1:        %{name}-rpmlintrc
Provides:       yast2-packagemanager
Obsoletes:      yast2-packagemanager

# Features we provide (update doc/autoinclude/FeatureTest.doc):
Provides:       libzypp(plugin) = 0
Provides:       libzypp(plugin:commit) = 0
Provides:       libzypp(plugin:services) = 0
Provides:       libzypp(plugin:system) = 0
Provides:       libzypp(plugin:urlresolver) = 0

%if 0%{?suse_version}
Recommends:     logrotate
# lsof is used for 'zypper ps':
Recommends:     lsof
%endif
BuildRequires:  cmake
BuildRequires:  openssl-devel
%if 0%{?suse_version} >= 1130
BuildRequires:  libudev-devel
%else
BuildRequires:  hal-devel
%endif
BuildRequires:  boost-devel
BuildRequires:  dejagnu
BuildRequires:  doxygen
BuildRequires:  gcc-c++
BuildRequires:  gettext-devel
BuildRequires:  graphviz
BuildRequires:  libxml2-devel
BuildRequires:  libproxy-devel

BuildRequires:  libsatsolver-devel >= 0.14.17
%if 0%{?suse_version} >= 1100
%requires_eq    satsolver-tools
%else
Requires:       satsolver-tools
%endif

# required for testsuite, webrick
BuildRequires:  ruby

%if 0%{?suse_version}
BuildRequires:  libexpat-devel
%else
BuildRequires:  expat-devel
%endif

%if 0%{?suse_version}
BuildRequires:  rpm-devel
Requires:       /usr/bin/uuidgen
%if 0%{?suse_version} > 1020
BuildRequires:  hicolor-icon-theme
%endif
%endif

%if 0%{?fedora_version}
BuildRequires:  glib2-devel
BuildRequires:  popt-devel
BuildRequires:  rpm-devel
%endif

%if 0%{?mandriva_version}
BuildRequires:  glib2-devel
BuildRequires:  librpm-devel
# uuidgen
Requires:       e2fsprogs
%endif

%if 0%{?suse_version}
Requires:       gpg2
%else
Requires:       gnupg2
%endif

%define min_aria_version 1.1.2
# ---------------------------------------------------------------
%if 0%{?suse_version} >= 1110
# (almost) common codebase, but on SLES11-SP1 (according to Rudi
# suse_version == 1110) we have a patched libcurl-7.19.0-11.22,
# and no aria2. Furthermore SLE may use it's own set of .po files
# from po/sle-zypp-po.tar.bz2.

# this check should use 7.19.0 if SLE and 7.19.4 if not (backported
# CURLOPT_REDIR_PROTOCOLS)
%define min_curl_version 7.19.0-11.22
%endif

# ---------------------------------------------------------------

%if 0%{?suse_version}
%if 0%{?suse_version} >= 1100
# Code11+
BuildRequires:  libcurl-devel >= %{min_curl_version}
Requires:       libcurl4   >= %{min_curl_version}
%else
# Code10
BuildRequires:  curl-devel
%endif
%else
# Other distros (Fedora)
BuildRequires:  libcurl-devel >= %{min_curl_version}
Requires:       libcurl   >= %{min_curl_version}
%endif

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
%if 0%{?suse_version} >= 1130
Requires:       libudev-devel
%else
Requires:       hal-devel
%endif
Requires:       cmake
%if 0%{?suse_version}
%if 0%{?suse_version} >= 1100
# Code11+
Requires:  libcurl-devel >= %{min_curl_version}
%else
# Code10
Requires:  curl-devel
%endif
%else
# Other distros (Fedora)
Requires:  libcurl-devel >= %{min_curl_version}
%endif
%if 0%{?suse_version} >= 1100
%requires_ge    libsatsolver-devel
%else
Requires:       libsatsolver-devel
%endif
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

%build
mkdir build
cd build
export CFLAGS="$RPM_OPT_FLAGS"
export CXXFLAGS="$RPM_OPT_FLAGS"
unset TRANSLATION_SET
# SLE11-* might want its own translation set:
%if 0%{?suse_version} == 1110
if [ -f ../po/sle-zypp-po.tar.bz ]; then
  export TRANSLATION_SET=sle-zypp
fi
%endif
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DDOC_INSTALL_DIR=%{_docdir} \
      -DLIB=%{_lib} \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SKIP_RPATH=1 \
      -DUSE_TRANSLATION_SET=${TRANSLATION_SET:-zypp} \
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
%if 0%{?fedora_version}
ln -s %{_sysconfdir}/yum.repos.d $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
%else
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
%endif
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zypp/services.d
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
%{find_lang} zypp


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

%clean
rm -rf "$RPM_BUILD_ROOT"

%files -f zypp.lang
%defattr(-,root,root)
%dir               %{_sysconfdir}/zypp
%if 0%{?fedora_version}
%{_sysconfdir}/zypp/repos.d
%else
%dir               %{_sysconfdir}/zypp/repos.d
%endif
%dir               %{_sysconfdir}/zypp/services.d
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
%defattr(-,root,root)
%{_libdir}/libzypp.so
%{_docdir}/%{name}
%{_includedir}/zypp
%{_datadir}/cmake/Modules/*
%{_libdir}/pkgconfig/libzypp.pc

%changelog
