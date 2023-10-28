#!/bin/bash
# Copyright (c) 2000-2017 Synology Inc. All rights reserved.

source /pkgscripts-ng/include/pkg_util.sh

package="uas"
version="0.1"
displayname="USB Attached SCSI storage driver"
maintainer="bb-qq"
arch="$(pkg_get_platform)"
install_type="package"
thirdparty="yes"

[ "$(caller)" != "0 NULL" ] && return 0

PRODUCT_VERSION_WITHOUT_MICRO=`echo ${PRODUCT_VERSION} | sed -E 's/^([0-9]+\.[0-9]+).+$/\1/'`
if [ 1 -eq $(echo "${PRODUCT_VERSION_WITHOUT_MICRO} >= 7.0" | bc) ]; then
    if [ 1 -eq $(echo "${PRODUCT_VERSION_WITHOUT_MICRO} >= 7.2" | bc) ]; then
        os_min_ver="7.2-64213"
    else
        os_min_ver="7.0-40000"
    fi
    RUN_AS="package"
    INSTRUCTION=' [DSM7 note] If this is the first time you are installing this driver, special steps are required. See the readme for details.'
else
    RUN_AS="root"
fi

cat <<EOS > `dirname $0`/conf/privilege
{
    "defaults": {
        "run-as": "${RUN_AS}"
    }
}
EOS

cat <<EOS > `dirname $0`/SynoBuildConf/depends
[default]
all="${PRODUCT_VERSION}"
EOS

description="USB Attached SCSI storage driver for USB Storage devices. ${INSTRUCTION}"

pkg_dump_info
