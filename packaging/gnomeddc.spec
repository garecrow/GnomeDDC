%global app_id io.github.gnomeddc.GnomeDDC

Name:           gnomeddc
Version:        0.1.0
Release:        1%{?dist}
Summary:        Adwaita native GNOME UI for ddcutil

License:        GPLv3+
URL:            https://github.com/GnomeDDC/GnomeDDC
Source0:        %{name}-%{version}.tar.gz

BuildArch:      noarch

BuildRequires:  python3-devel
BuildRequires:  python3dist(pyproject-hooks)
BuildRequires:  python3dist(setuptools)
BuildRequires:  python3dist(wheel)
BuildRequires:  desktop-file-utils

Requires:       python3dist(PyGObject)
Requires:       gtk4 >= 4.10
Requires:       libadwaita >= 1.4
Requires:       ddcutil

%description
GnomeDDC is a native Libadwaita application that wraps ddcutil to offer
brightness, contrast, and input switching controls for external displays directly
from GNOME. The interface is designed for Wayland systems but can fall back to
X11 when necessary.

%prep
%autosetup -n %{name}-%{version}

%generate_buildrequires
%pyproject_buildrequires

%build
%pyproject_wheel

%install
%pyproject_install
%pyproject_save_files gnomeddc

install -Dm0644 data/%{app_id}.desktop \
    %{buildroot}%{_datadir}/applications/%{app_id}.desktop
install -Dm0644 data/icons/hicolor/scalable/apps/%{app_id}.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{app_id}.desktop

%files -f %{pyproject_files}
%doc README.md
%{_bindir}/gnomeddc
%{_datadir}/applications/%{app_id}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg

%changelog
* Tue Jul 02 2024 GnomeDDC Maintainers <maintainers@gnomeddc.invalid> - 0.1.0-1
- Initial package
