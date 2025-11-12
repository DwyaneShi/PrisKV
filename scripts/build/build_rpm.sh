#!/bin/bash

set -ex

OUPUT_DIR=$1

RPM_TOPDIR=${HOME}/rpmbuild

rpmdev-setuptree

tar -cvf ${RPM_TOPDIR}/SOURCES/priskv.tar --transform 's,^,priskv/,' .
cp redhat/priskv.spec ${RPM_TOPDIR}/SPECS/

rpmbuild -bb ${RPM_TOPDIR}/SPECS/priskv.spec

mv ${RPM_TOPDIR}/RPMS/x86_64/priskv-*.rpm ${OUPUT_DIR}/
rm -rf ${RPM_TOPDIR}
