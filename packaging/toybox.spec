Summary: Single binary providing simplified versions of system commands
Name: toybox
Version: 0.4.4
Release: 1
License: BSD
Group: System/Shells
Source: http://www.landley.net/toybox/downloads/%{name}-%{version}.tar.bz2
Source1: toybox_tizen.config
Source2: bin.links
Source3: sbin.links
Source4: usrbin.links
Source5: usrsbin.links
Source101: klogd.service
Source102: syslogd.service
Source1001: toybox.manifest
Source1002: syslogd.manifest

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

%package symlinks-udhcpc
Group: tools
Summary: ToyBox symlinks to provide 'udhcpc'
Requires: %{name} = %{version}-%{release}

%description symlinks-udhcpc
ToyBox symlinks for utilities corresponding to 'udhcpc' package.

%package symlinks-udhcpd
Group: tools
Summary: ToyBox symlinks to provide 'udhcpd'
Requires: %{name} = %{version}-%{release}

%description symlinks-udhcpd
ToyBox symlinks for utilities corresponding to 'udhcpd' package.

%prep
%setup -q

%build
cp %{SOURCE1001} .
cp %{SOURCE1002} .
# create dynamic toybox - the executable is toybox
make defconfig
cp packaging/toybox_tizen.config .config
# In case of user image, TIZEN_SECURE_MOUNT will be defined for secure mount.
%if "%{?sec_build_project_name}" == "redwood8974_jpn_dcm"
%if 0%{?tizen_build_binary_release_type_eng} != 1
export CFLAGS+=" -DTIZEN_SECURE_MOUNT"
%endif
%endif
make -j 4 CC="gcc $RPM_OPT_FLAGS"
cp toybox toybox-dynamic

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/bin
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

mkdir -p $RPM_BUILD_ROOT%{_datadir}/license
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-klogd
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-sysklogd
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-udhcpc
cat LICENSE > $RPM_BUILD_ROOT%{_datadir}/license/toybox-symlinks-udhcpd

%files
%defattr(-,root,root,-)
%doc LICENSE
%{_datadir}/license/toybox
/bin/toybox
/bin/mount
/bin/umount
%manifest toybox.manifest

%files symlinks-klogd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-klogd
/sbin/klogd
%{_libdir}/systemd/system/klogd.service
%{_libdir}/systemd/system/basic.target.wants/klogd.service
%manifest toybox.manifest

%files symlinks-sysklogd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-sysklogd
/sbin/syslogd
%{_libdir}/systemd/system/syslogd.service
%{_libdir}/systemd/system/basic.target.wants/syslogd.service
%manifest syslogd.manifest

%files symlinks-udhcpc
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-udhcpc
%{_bindir}/udhcpc
%manifest toybox.manifest

%files symlinks-udhcpd
%defattr(-,root,root,-)
%{_datadir}/license/toybox-symlinks-udhcpd
%{_bindir}/dumpleases
%{_sbindir}/udhcpd
%manifest toybox.manifest
