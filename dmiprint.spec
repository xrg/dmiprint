%define git_repo dmiprint

# inhibit the debug package (or else we'd need -g in CFLAGS)
%define debug_package %{nil}

Name:       dmiprint
Summary:    Simple utility to decode DMI type 9 table
Version:    %git_get_ver
Release:    %mkrel %git_get_rel
URL:        https://github.com/xrg/dmiprint
Source0:    %git_bs_source %{name}-%{version}.tar.gz
License:    MIT
Group:      System/Kernel and hardware


%description
A simple, minimal DMI decoder for type 9 table entries. These are
the ones mapping addresses (PCI BDFs) to the physical slot name.

Rather than using `dmidecode` , this only minds decoding the slot
designator, prints it in a script-friendly way.


%prep
%git_get_source
%setup -q


%build
cd src
%make_build


%install
install -d %{buildroot}%{_sbindir}/
install src/dmiprint %{buildroot}%{_sbindir}/
install -d %{buildroot}%{_udevrulesdir}
cp -ar rules.d/* %{buildroot}%{_udevrulesdir}/


%files
%attr(0755,root,root) %{_sbindir}/dmiprint
%defattr(-,root,root)
%doc README.md
%{_udevrulesdir}/50-pcislot.rules


%changelog -f %{_sourcedir}/%{name}-changelog.gitrpm.txt

