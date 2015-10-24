Summary: Single binary providing simplified versions of system commands
Name: toybox
Version: 0.6.0
Release: 1
License: BSD
Group: System/Shells
Source: http://www.landley.net/toybox/downloads/%{name}-%{version}.tar.bz2
Source1: toybox_tizen.config
%if "%{?tizen_profile_name}"=="tv"
Source2: bin_tv.links
Source3: sbin_tv.links
Source4: usrbin_tv.links
%else
Source2: bin.links
Source3: sbin.links
Source4: usrbin.links
%endif
Source5: usrsbin.links
Source101: klogd.service
Source102: syslogd.service
Source1001: toybox.manifest
Source1002: syslogd.manifest
Source1003: klogd.manifest

URL: http://www.landley.net/toybox/

%description 
Toybox is a single binary which includes versions of a large number
of system commands, including a shell.  This package can be very
useful for recovering from certain types of system failures,
particularly those involving broken shared libraries.

%package symlinks-klogd
Group: tools
Summary: ToyBox symlinks to provide 'klogd'
Requires: %{name} = %{version}-%{release}

%description symlinks-klogd
ToyBox symlinks for utilities corresponding to 'klogd' package.

%package symlinks-sysklogd
Group: tools
Summary: ToyBox symlinks to provide 'sysklogd'
Requires: %{name} = %{version}-%{release}

%description symlinks-sysklogd
ToyBox symlinks for utilities corresponding to 'sysklogd' package.

%package symlinks-dhcp
Group: tools
Summary: ToyBox symlinks to provide 'dhcp'
Requires: %{name} = %{version}-%{release}

%description symlinks-dhcp
ToyBox symlinks for utilities corresponding to 'dhcp' package.

%package symlinks-dhcpd
Group: tools
Summary: ToyBox symlinks to provide 'dhcpd'
Requires: %{name} = %{version}-%{release}

%description symlinks-dhcpd
ToyBox symlinks for utilities corresponding to 'dhcpd' package.

%prep
%setup -q

%build
cp %{SOURCE1001} .
cp %{SOURCE1002} .
cp %{SOURCE1003} .
# create dynamic toybox - the executable is toybox
make defconfig
cp %{SOURCE1} .config
# In case of user image, TIZEN_SECURE_MOUNT will be defined for secure mount.
%if "%{?sec_build_project_name}" == "redwood8974_jpn_dcm"
%if 0%{?tizen_build_binary_release_type_eng} != 1
export CFLAGS+=" -DTIZEN_SECURE_MOUNT"
%endif
%endif
make -j 4 CC="gcc $RPM_OPT_FLAGS" CFLAGS="$CFLAGS -fPIE" LDOPTIMIZE="-Wl,--gc-sections -pie"
cp toybox toybox-dynamic

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/bin
mkdir -p $RPM_BUILD_ROOT/sbin
install -m 755 toybox-dynamic $RPM_BUILD_ROOT/bin/toybox

# debian/toybox.links
pushd %{buildroot}
mkdir -p usr/bin usr/sbin sbin
cd bin
for f in `cat %SOURCE2` ; do ln -s toybox $f ; done
cd ../sbin
for f in `cat %SOURCE3` ; do ln -s ../bin/toybox $f ; done
cd ../usr/bin
for f in `cat %SOURCE4` ; do ln -s ../../bin/toybox $f ; done
cd ../../usr/sbin
for f in `cat %SOURCE5` ; do ln -s ../../bin/toybox $f ; done
popd

# install systemd service files for syslogd and klogd
mkdir -p %{buildroot}%{_libdir}/systemd/system/basic.target.wants
install -m 644 %SOURCE101 %{buildroot}%{_libdir}/systemd/system/klogd.service
ln -s ../klogd.service %{buildroot}%{_libdir}/systemd/system/basic.target.wants/klogd.service
install -m 644 %SOURCE102 %{buildroot}%{_libdir}/systemd/system/syslogd.service
ln -s ../syslogd.service %{buildroot}%{_libdir}/systemd/system/basic.target.wants/syslogd.service
rm -rf $RPM_BUILD_ROOT/sbin/syslogd
cp -f $RPM_BUILD_ROOT/bin/toybox $RPM_BUILD_ROOT/sbin/syslogd
rm -rf $RPM_BUILD_ROOT/sbin/klogd
cp -f $RPM_BUILD_ROOT/bin/toybox $RPM_BUILD_ROOT/sbin/klogd

mkdir -p $RPM_BUILD_ROOT%{_datadir}/license
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-klogd
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-sysklogd
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-dhcp
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-dhcpd

%files
%defattr(-,root,root,-)
%doc LICENSE
%{_datadir}/license/toybox
/bin/toybox
%if "%{?tizen_profile_name}"=="tv"
/sbin/ping
/bin/ping
/sbin/ping6
/bin/ping6
#/usr/bin/nslookup
%endif
%manifest toybox.manifest

%files symlinks-klogd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-klogd
/sbin/klogd
%{_libdir}/systemd/system/klogd.service
%{_libdir}/systemd/system/basic.target.wants/klogd.service
%manifest klogd.manifest

%files symlinks-sysklogd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-sysklogd
/sbin/syslogd
%{_libdir}/systemd/system/syslogd.service
%{_libdir}/systemd/system/basic.target.wants/syslogd.service
%manifest syslogd.manifest

%files symlinks-dhcp
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-dhcp
%{_bindir}/dhcp
%manifest toybox.manifest

%files symlinks-dhcpd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-dhcpd
%if "%{?tizen_profile_name}"!="tv"
%{_bindir}/dumpleases
%endif
%{_sbindir}/dhcpd
%manifest toybox.manifest
