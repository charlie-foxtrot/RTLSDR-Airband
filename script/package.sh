#!/bin/bash

set -o errexit
set -o noclobber
set -o nounset
set -o pipefail

if [ -z "$1" ]
  then
    echo "Usage: $0 package_root [ compress_type ]"
    echo "compress_type must be one of the types accepted by dpkg-deb (-Z), default to 'xz'"
    exit 1
fi

readonly package_root="$1"
readonly compress_type="${2:-xz}"

# calculate uncompressed size in kB, rounded up
package_root_size="$(du --bytes --summarize --exclude=DEBIAN "${package_root}"/ | awk '{print $1}')"
readonly package_root_size
readonly installed_size="$(( (package_root_size + 1024 - 1) / 1024 ))"

# creates control file
mkdir -p "${package_root}/DEBIAN/"
cat << EOF | envsubst > "${package_root}/DEBIAN/control"
Package: ${PACKAGE}
Version: ${VERSION}
Installed-Size: ${installed_size}
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Depends: ${DEPENDS}
Description: ${DESCRIPTION}
Homepage: https://github.com/charlie-foxtrot/RTLSDR-Airband.git
EOF

# post install script to symlink default config
cat << EOF > "${package_root}/DEBIAN/postinst"
#!/bin/sh

set -o errexit

if [ ! -e /usr/share/rtl_airband/config/default.conf ]; then
  ln --symbolic /usr/share/rtl_airband/config/basic_emergency.conf /usr/share/rtl_airband/config/default.conf
fi
EOF

# pre remove script to delete postinst symlink
cat << EOF > "${package_root}/DEBIAN/prerm"
#!/bin/sh

set -o errexit

if [ -L /usr/share/rtl_airband/config/default.conf ]; then
  rm /usr/share/rtl_airband/config/default.conf
fi
EOF

chmod 755 "${package_root}/DEBIAN/postinst" "${package_root}/DEBIAN/prerm"


# creates package structure
mkdir --parents "${package_root}/lib/systemd/system/"
sed 's|/usr/local/bin|/usr/bin|g' init.d/rtl_airband.service > "${package_root}/lib/systemd/system/rtl_airband.service"
mkdir --parents "${package_root}/usr/bin"
cp build_Release_NFM/src/rtl_airband "${package_root}/usr/bin/"
mkdir --parents "${package_root}/usr/share/rtl_airband/config"
cp config/*.conf "${package_root}/usr/share/rtl_airband/config/"
mkdir --parents "${package_root}/usr/share/doc/rtl_airband"
cp README.md LICENSE "${package_root}/usr/share/doc/rtl_airband/"
gzip --keep --stdout NEWS.md > "${package_root}/usr/share/doc/rtl_airband/NEWS.md.gz"

# package
readonly DEB_FILE="${PACKAGE}_${VERSION}_${ARCH}.deb"
dpkg-deb -Z"${compress_type}" --root-owner-group --build "${package_root}" "$DEB_FILE"
